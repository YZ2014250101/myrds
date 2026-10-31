#include "kv_store.h"
#include "../portocol/resp_parser.h"
#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <unistd.h>

KVStore::KVStore() {
    loadRdb();
    // 构造函数末尾，加载AOF
    loadAof();
}

KVStore::~KVStore() {
    if (aof_fd_ >= 0) {
        ::close(aof_fd_);
        aof_fd_ = -1;
    }
}
bool KVStore::dictSet(const std::string& key, const std::string& value, uint64_t expire_at_ms) {
    dict_.set(key, value, expire_at_ms);
    return true;
}

bool KVStore::dictGet(const std::string& key, std::string& out_val) { return dict_.get(key, out_val); }

bool KVStore::dictDel(const std::string& key) { return dict_.del(key); }

bool KVStore::dictExists(const std::string& key) { return dict_.exists(key); }

bool KVStore::dictExpire(const std::string& key, int64_t seconds) { return dict_.expire(key, seconds); }

int64_t KVStore::dictTtl(const std::string& key) { return dict_.ttl(key); }

std::vector<std::string> KVStore::dictKeys() { return dict_.keys(); }
int KVStore::init(int max_level) { return _sl.listInit(max_level); }

int KVStore::set(const std::string& key, const std::string& value) {
    // 直接调用跳表insert，跳表内部支持覆写更新value
    return _sl.insert(key, value);
}

bool KVStore::get(const std::string& key, std::string& out_val) {
    Node* node = _sl.find(key);
    if (node == nullptr) {
        return false;
    }
    out_val = node->value;
    return true;
}

int KVStore::del(const std::string& key) { return _sl.erase(key); }

bool KVStore::exists(const std::string& key) { return _sl.find(key) != nullptr; }

int KVStore::size() const { return _sl.getNodeCnt(); }

void KVStore::print() const { _sl.print(); }

int KVStore::splitTokens(std::vector<std::string>& tokens, const std::string& msg) {
    tokens.clear();
    size_t pos = 0;
    while (pos < msg.size()) {
        // 跳过空格
        while (pos < msg.size() && msg[pos] == ' ')
            pos++;
        if (pos >= msg.size())
            break;

        // ========== 碰到 \r \n 直接停止解析 ==========
        if (msg[pos] == '\r' || msg[pos] == '\n') {
            break;
        }

        size_t next_pos = msg.find(' ', pos);
        if (next_pos == std::string::npos) {
            next_pos = msg.size();
        }
        // 检查这个token中间有没有换行（防止换行在token里面）
        size_t cr = msg.find('\r', pos);
        size_t lf = msg.find('\n', pos);
        if (cr != std::string::npos && cr < next_pos) {
            next_pos = cr;
        }
        if (lf != std::string::npos && lf < next_pos) {
            next_pos = lf;
        }

        tokens.push_back(msg.substr(pos, next_pos - pos));
        pos = next_pos + 1;
    }
    return tokens.size();
}

int KVStore::parseCmd(const std::vector<std::string> tokens, int len) {
    if (len == 0) {
        return -1;
    }
    std::string cmd = tokens[0];
    for (char& c : cmd) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    if (cmd == "set") {
        return SET;
    }
    if (cmd == "get") {
        return GET;
    }
    if (cmd == "del") {
        return DEL;
    }
    if (cmd == "exists") {
        return EXISTS;
    }
    if (cmd == "expire") {
        return EXPIRE;
    }
    if (cmd == "ttl") {
        return TTL;
    }
    if (cmd == "keys") {
        return KEYS;
    }
    return UNKNOWN;
}

std::string KVStore::protocal(const char* buffer, size_t n) {
    std::string raw_resp(buffer, n);
    std::string msg(buffer, n);
    std::vector<std::string> tokens;
    int num_tokens = splitTokens(tokens, msg);
    int user_cmd = parseCmd(tokens, num_tokens);
    std::string out_val;
    switch (user_cmd) {
    case SET:
        if (tokens.size() != 3) {
            out_val = "SET need key value";
            out_val = resp_error(out_val);
            break;
        }
        if (dictSet(tokens[1], tokens[2])) {
            out_val = "OK";
            out_val = resp_simple_string(out_val);
            appendAof(raw_resp);
        } else {
            out_val = "set fail";
            out_val = resp_error(out_val);
        }
        break;
    case GET: {
        if (tokens.size() != 2) {
            out_val = "GET need key";
            out_val = resp_error(out_val);
            break;
        }
        std::string val;
        if (dictGet(tokens[1], val)) {
            // GET 标准用bulk字符串
            out_val = resp_bulk_string(val);
        } else {
            out_val = resp_null();
        }
        break;
    }
    case DEL:
        if (tokens.size() != 2) {
            out_val = "DEL need key";
            out_val = resp_error(out_val);
            break;
        }
        if (dictDel(tokens[1])) {
            out_val = resp_integer(1);
            appendAof(raw_resp);
        } else {
            out_val = resp_integer(0);
        }
        break;
    case EXISTS:
        if (tokens.size() != 2) {
            out_val = "EXISTS need key";
            out_val = resp_error(out_val);
            break;
        }
        if (dictExists(tokens[1])) {
            out_val = resp_integer(1);
        } else {
            out_val = resp_integer(0);
        }
        break;
    case EXPIRE:
        if (tokens.size() != 3) {
            out_val = "EXPIRE need key seconds";
            out_val = resp_error(out_val);
            break;
        }
        if (dictExpire(tokens[1], std::stoi(tokens[2]))) {
            out_val = resp_integer(1);
            appendAof(raw_resp);
        } else {
            out_val = resp_integer(0);
        }
        break;
    case TTL:
        if (tokens.size() != 2) {
            out_val = "TTL need key";
            out_val = resp_error(out_val);
            break;
        }
        // 删掉手动 + "\n"！！
        out_val = resp_integer(dictTtl(tokens[1]));
        break;
    case KEYS: {
        // KEYS 要返回 RESP数组 *N，你现在简单bulk先凑合用，后面再改数组
        std::string tmp;
        for (const auto& key : dictKeys()) {
            tmp += key + " ";
        }
        out_val = resp_bulk_string(tmp);
        break;
    }
    case UNKNOWN:
    default:
        bool ok = saveRdb();
        if (ok) {
            // 返回ok给客户端
            out_val += "OK";
        } else {
            out_val += "save fail";
        }
        out_val = "unknown cmd";
        out_val = resp_error(out_val);
        break;
    }
    return out_val;
}
// 加载AOF：打开aof.log，循环读取、解析、回放命令
bool KVStore::loadAof() {
    if (!aof_enable_) {
        return true;
    }

    std::ifstream fin("aof.log");
    if (fin.is_open()) {
        std::string line;
        while (std::getline(fin, line)) {
            if (line.empty())
                continue;
            fprintf(stderr, "[AOF] replay: %s\n", line.c_str());
            protocal(line.data(), line.size());
        }
        fin.close();
        fprintf(stderr, "[AOF] load aof done\n");
    } else {
        fprintf(stderr, "[AOF] aof.log not exist, skip load\n");
    }

    // 打开追加写句柄，供后续appendAof使用
    aof_fd_ = open("aof.log", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (aof_fd_ < 0) {
        fprintf(stderr, "[AOF] open aof write fail: %s\n", strerror(errno));
        aof_enable_ = false;
        return false;
    }
    return true;
}

// 追加一条原始RESP命令到AOF
void KVStore::appendAof(const std::string& cmd_line) {
    if (!aof_enable_ || aof_fd_ < 0)
        return;
    std::string line = cmd_line + "\n";
    write(aof_fd_, line.data(), line.size());
}
std::string KVStore::CmdToRawString(const RespCmd& cmd) {
    std::string out = cmd.cmd;
    for (auto& arg : cmd.args) {
        out += " ";
        out += arg;
    }
    return out;
}
bool KVStore::saveRdb(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) {
        fprintf(stderr, "[RDB] save open %s failed\n", path.c_str());
        return false;
    }

    auto keys = dict_.keys();
    uint64_t now
        = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
              .count();

    for (auto& k : keys) {
        std::string v;
        // 拿value和过期时间
        DictEntry* e = dict_.findEntry(dict_.getHashTable(0), k);
        if (!e)
            continue;
        // 过期key跳过
        if (e->expire_ts != 0 && e->expire_ts < now)
            continue;

        // 写入格式：
        // key_len(uint32) + key_data
        // val_len(uint32) + val_data
        // expire_ts(uint64)
        uint32_t klen = (uint32_t)k.size();
        uint32_t vlen = (uint32_t)e->val.size();
        fwrite(&klen, sizeof(klen), 1, fp);
        fwrite(k.data(), 1, klen, fp);
        fwrite(&vlen, sizeof(vlen), 1, fp);
        fwrite(e->val.data(), 1, vlen, fp);
        fwrite(&e->expire_ts, sizeof(uint64_t), 1, fp);
    }

    fclose(fp);
    fprintf(stderr, "[RDB] save success, key count: %zu\n", keys.size());
    return true;
}

// 加载RDB快照
bool KVStore::loadRdb(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) {
        fprintf(stderr, "[RDB] dump.rdb not found, skip rdb load\n");
        return true;
    }

    uint32_t klen, vlen;
    uint64_t expire_ts;
    size_t cnt = 0;

    while (true) {
        // 读key长度
        if (fread(&klen, sizeof(klen), 1, fp) != 1)
            break;
        // 读key
        std::string key(klen, 0);
        if (fread(&key[0], 1, klen, fp) != klen)
            break;

        // 读value长度
        if (fread(&vlen, sizeof(vlen), 1, fp) != 1)
            break;
        std::string val(vlen, 0);
        if (fread(&val[0], 1, vlen, fp) != vlen)
            break;

        // 读过期时间
        if (fread(&expire_ts, sizeof(uint64_t), 1, fp) != 1)
            break;

        dictSet(key, val, expire_ts);
        cnt++;
    }
    fclose(fp);
    fprintf(stderr, "[RDB] load finish, loaded %zu keys\n", cnt);
    return true;
}
