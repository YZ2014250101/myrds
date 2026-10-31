#include "kv_store.h"
#include "../portocol/resp_parser.h"
KVStore::KVStore() { }

KVStore::~KVStore() {
    // Skiplist析构会自动释放所有节点，不用额外写
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
            out_val = "set ok";
            out_val = resp_simple_string(out_val);
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
            out_val = "get " + val;
            out_val = resp_simple_string(out_val);
        } else {
            out_val = "not found";
            out_val = resp_error(out_val);
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
            out_val = "del ok";
            out_val = resp_simple_string(out_val);
        } else {
            out_val = "del fail";
            out_val = resp_error(out_val);
        }
        break;
    case EXISTS:
        if (tokens.size() != 2) {
            out_val = "EXISTS need key";
            out_val = resp_error(out_val);
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
        }
        if (dictExpire(tokens[1], std::stoi(tokens[2]))) {
            out_val = "expire ok";
            out_val = resp_simple_string(out_val);
        } else {
            out_val = "expire fail";
            out_val = resp_error(out_val);
        }
        break;
    case TTL:
        if (tokens.size() != 2) {
            out_val = "TTL need key";
            out_val = resp_error(out_val);
        }
        out_val = "ttl " + std::to_string(dictTtl(tokens[1]));
        out_val = resp_simple_string(out_val);
        break;
    case KEYS:
        out_val = "keys ";
        for (const auto& key : dictKeys()) {
            out_val += key + " ";
        }
        out_val = resp_bulk_string(out_val);
        break;
    case UNKNOWN:
        out_val = "unknown cmd";
        out_val = resp_error(out_val);
        break;
    default:
        out_val = "unknown cmd";
        out_val = resp_error(out_val);
        break;
    }
    return out_val;
}
