#include "kv_store.h"
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

std::string KVStore::protocal(char* buffer, size_t n) {
    std::string msg(buffer, n);
    std::vector<std::string> tokens;
    int num_tokens = splitTokens(tokens, msg);
    int user_cmd = parseCmd(tokens, num_tokens);

    std::string out_val;
    switch (user_cmd) {
    case SET:
        if (tokens.size() != 3) {
            out_val = "err: SET need key value";
            break;
        }
        if (dictSet(tokens[1], tokens[2])) {
            out_val = "set ok";
        } else {
            out_val = "set fail";
        }
        break;
    case GET: {
        if (tokens.size() != 2) {
            out_val = "err: GET need key";
            break;
        }
        std::string val;
        if (dictGet(tokens[1], val)) {
            out_val = "get " + val;
        } else {
            out_val = "not found";
        }
        break;
    }
    case DEL:
        if (tokens.size() != 2) {
            out_val = "err: DEL need key";
            break;
        }
        if (dictDel(tokens[1])) {
            out_val = "del ok";
        } else {
            out_val = "del fail";
        }
        break;
    case EXISTS:
        if (tokens.size() != 2) {
            out_val = "err: EXISTS need key";
        }
        if (dictExists(tokens[1])) {
            out_val = "your key exists";
        } else {
            out_val = "your key not exists";
        }
        break;
    case EXPIRE:
        if (tokens.size() != 3) {
            out_val = "err: EXPIRE need key seconds";
        }
        if (dictExpire(tokens[1], std::stoi(tokens[2]))) {
            out_val = "expire ok";
        } else {
            out_val = "expire fail";
        }
        break;
    case TTL:
        if (tokens.size() != 2) {
            out_val = "err: TTL need key";
        }
        out_val = "ttl " + std::to_string(dictTtl(tokens[1])) + "\n";
        break;
    case KEYS:
        out_val = "keys ";
        for (const auto& key : dictKeys()) {
            out_val += key + " ";
        }
        break;
    case UNKNOWN:
        out_val = "unknown cmd";
        break;
    default:
        out_val = "unknown cmd";
        break;
    }
    out_val += "\n";
    return out_val;
}
