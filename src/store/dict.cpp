#include "dict.h"
#include <chrono>
#include <iostream>

HashTable::HashTable()
    : table(nullptr)
    , size(0)
    , used(0) { }
HashTable::~HashTable() { clear(); }
bool HashTable::init(size_t sz) {
    if (sz & (sz - 1)) {
        std::cerr << "hash table size must be power of 2" << std::endl;
        return false;
    }
    if (table != nullptr) {
        std::cerr << "hashtable already inited!" << std::endl;
        return false;
    }
    table = new DictEntry* [sz] { };
    size = sz;
    return true;
}
void HashTable::freeEntry(DictEntry* node) {
    if (node) {
        delete node;
    }
}
void HashTable::clear() {
    if (table) {
        for (int i = 0; i < size; i++) {
            DictEntry* cur = table[i];
            while (cur) {
                DictEntry* next = cur->next;
                freeEntry(cur);
                cur = next;
            }
        }
        delete[] table;
        table = nullptr;
        size = 0;
        used = 0;
    }
}
void HashTable::resetTable() {
    if (table) {
        delete[] table;
        table = nullptr;
        size = 0;
        used = 0;
    }
}
Dict::Dict()
    : rehash_idx(-1) {
    ht[0].init(INIT_SIZE);
}
Dict::~Dict() {
    ht[0].clear();
    ht[1].clear();
}
// 设置键值对，expire_ms：0代表不过期
void Dict::set(const std::string& key, const std::string& value, uint64_t expire_ms) {
    DictEntry* e = findEntry(&ht[0], key);
    if (e) {
        e->val = value;
        e->expire_ts = expire_ms;
        return;
    }
    if (needExpand()) {
        size_t new_size = ht[0].size * 2;
        HashTable new_ht;
        new_ht.init(new_size);
        for (size_t i = 0; i < ht[0].size; i++) {
            DictEntry* cur = ht[0].table[i];
            while (cur) {
                DictEntry* next = cur->next;
                size_t idx = hashKey(cur->key) & (new_size - 1);
                cur->next = new_ht.table[idx];
                new_ht.table[idx] = cur;
                new_ht.used++;
                cur = next;
                ht[0].used--;
            }
        }
        ht[0].resetTable();
        ht[0] = std::move(new_ht);
    }
    size_t idx = hashKey(key) & (ht[0].size - 1);
    DictEntry* new_node = new DictEntry { key, value, expire_ms };
    new_node->next = ht[0].table[idx];
    ht[0].table[idx] = new_node;
    ht[0].used++;
}

// 获取键值对，返回true表示存在；同时惰性删除：如果已经过期，直接删、返回false
bool Dict::get(const std::string& key, std::string& out_val) {
    DictEntry* e = findEntry(&ht[0], key);
    if (!e) {
        return false;
    }
    uint64_t now
        = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
              .count();

    if (e->expire_ts && e->expire_ts < now) {
        del(key);
        return false;
    }
    out_val = e->val;
    return true;
}

// 删除键值对，返回true表示存在
bool Dict::del(const std::string& key) {
    size_t h = hashKey(key) & (ht[0].size - 1);
    DictEntry* cur = ht[0].table[h];
    DictEntry* pre = nullptr;
    while (cur) {
        if (cur->key == key) {
            if (pre) {
                pre->next = cur->next;
            } else {
                ht[0].table[h] = cur->next;
            }
            delete cur;
            ht[0].used--;
            return true;
        }
        pre = cur;
        cur = cur->next;
    }
    return false;
}

// 判断key是否存在（同样惰性删过期key）
bool Dict::exists(const std::string& key) {
    DictEntry* e = findEntry(&ht[0], key);
    if (!e) {
        return false;
    }
    uint64_t now
        = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
              .count();

    if (e->expire_ts && e->expire_ts < now) {
        del(key);
        return false;
    }
    return true;
}

// 设置过期时间，单位秒
bool Dict::expire(const std::string& key, uint64_t expire_sec) {
    DictEntry* cur = findEntry(&ht[0], key);
    while (cur) {
        if (cur->key == key) {
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                              .count();
            uint64_t expire_at = static_cast<uint64_t>(now_ms + expire_sec * 1000);
            cur->expire_ts = expire_at;
            return true;
        }
        cur = cur->next;
    }
    return false;
}

// 获取key剩余ttl毫秒；返回-1永不过期，-2不存在/已过期
int64_t Dict::ttl(const std::string& key) {
    DictEntry* e = findEntry(&ht[0], key);
    if (!e)
        return -2;
    uint64_t now
        = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
              .count();
    if (e->expire_ts == 0)
        return -1;
    if (now > e->expire_ts) {
        del(key);
        return -2;
    }
    return static_cast<int64_t>(e->expire_ts - now);
}

// 遍历所有key（给KEYS命令用，简单版，不做scan游标）
std::vector<std::string> Dict::keys() {
    std::vector<std::string> res;
    for (int i = 0; i < ht[0].size; i++) {
        DictEntry* cur = ht[0].table[i];
        while (cur) {
            uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                               .count();
            if (!cur->expire_ts || cur->expire_ts > now) {
                res.push_back(cur->key);
            }
            cur = cur->next;
        }
    }
    return res;
}

// 主动尝试rehash一步；未实现渐进rehash，直接一次性rehash
void Dict::rehashStep() { }

// 哈希函数
size_t Dict::hashKey(const std::string& key) {
    // 简易哈希，生产别用；只是学习
    return std::hash<std::string> { }(key);
}

// 在一张ht里查找entry
DictEntry* Dict::findEntry(HashTable* ht, const std::string& key) {
    size_t idx = hashKey(key) & (ht->size - 1);
    DictEntry* cur = ht->table[idx];
    while (cur) {
        if (cur->key == key) {
            return cur;
        }
        cur = cur->next;
    }
    return nullptr;
}

// 扩容触发判断
bool Dict::needExpand() {
    if (rehash_idx != -1) {
        return false;
    }
    return static_cast<double>(ht[0].used) / ht[0].size >= LOAD_FACTOR;
}

std::vector<DictEntry> Dict::getAllValidEntries() {
    std::vector<DictEntry> res;
    uint64_t now
        = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
              .count();
    // rehash 期间 ht[0]、ht[1] 都要遍历
    for (int table_idx = 0; table_idx < 2; table_idx++) {
        HashTable* ht = &this->ht[table_idx];
        if (ht->table == nullptr)
            continue;

        for (size_t i = 0; i < ht->size; i++) {
            DictEntry* cur = ht->table[i];
            while (cur != nullptr) {
                DictEntry* next = cur->next; // 先存next，防止删的时候断链
                if (cur->expire_ts == 0 || cur->expire_ts > now) {
                    res.push_back(*cur);
                }
                cur = next;
            }
        }
    }
    return res;
}
