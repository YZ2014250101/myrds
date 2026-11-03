#include "dict.h"
#include "../util/util.h"
#include <cstddef>
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
    if (!node) {
        return;
    }
    // 有内存池：不 delete，头插挂回 Dict 的 DictEntry 空闲链，等 newEntry 复用。
    // 节点里 key/val 的 std::string 已分配的字符串内存也一并保留，复用时通常不再分配
    if (free_list_head_ != nullptr) {
        node->next = *free_list_head_;
        *free_list_head_ = node;
        return;
    }
    // 没挂接内存池（独立使用的 HashTable）：保持原来的真正释放行为
    delete node;
}
void HashTable::clear() {
    if (table) {
        for (size_t i = 0; i < size; i++) {
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
    // 两张表共用同一条空闲链：把“头指针的地址”借给它们，freeEntry 才能直接回收
    ht[0].free_list_head_ = &entry_free_list;
    ht[1].free_list_head_ = &entry_free_list;
    ht[0].init(INIT_SIZE);
}
Dict::~Dict() {
    // 先清两张表：还挂在桶上的节点会被 freeEntry 回收进空闲链
    ht[0].clear();
    ht[1].clear();
    // 再真正 delete 掉整条空闲链，防止内存泄漏
    DictEntry* cur = entry_free_list;
    while (cur != nullptr) {
        DictEntry* next = cur->next;
        delete cur;
        cur = next;
    }
    entry_free_list = nullptr;
}
// 设置键值对，expire_ms：0代表不过期
void Dict::set(const std::string& key, const std::string& value, uint64_t expire_ms) {
    DictEntry* e = findEntry(key);
    if (e) {
        e->val = value;
        e->expire_ts = expire_ms;
        return;
    }
    if (needExpand()) {
        size_t new_size = ht[0].size * 2;
        ht[1].init(new_size);
        rehash_idx = 0;
        ht_idx = 1; // 新的ht是ht[1]
    }
    if (rehash_idx != -1) {
        rehashStep();
    }
    size_t idx = hashKey(key) & (ht[ht_idx].size - 1);
    uint64_t expire_at = expire_ms > 0 ? getNow() + expire_ms : 0;
    // 优先复用空闲链里的节点，没有才分配新节点
    DictEntry* new_node = newEntry(key, value, expire_at);

    new_node->next = ht[ht_idx].table[idx];
    ht[ht_idx].table[idx] = new_node;
    ht[ht_idx].used++;
}

// 获取键值对，返回true表示存在；同时惰性删除：如果已经过期，直接删、返回false
bool Dict::get(const std::string& key, std::string& out_val) {
    if (rehash_idx != -1) {
        rehashStep();
    }
    DictEntry* e = findEntry(key);
    if (!e) {
        return false;
    }
    out_val = e->val;
    return true;
}

// 删除键值对，返回true表示存在
bool Dict::del(const std::string& key) {
    size_t h = hashKey(key) & (ht[ht_idx].size - 1);
    DictEntry* cur = ht[ht_idx].table[h];
    DictEntry* pre = nullptr;
    while (cur) {
        if (cur->key == key) {
            if (pre) {
                pre->next = cur->next;
            } else {
                ht[ht_idx].table[h] = cur->next;
            }
            // 回收进内存池而不是 delete；对外语义与原来完全一致（节点已摘链、used--）
            ht[ht_idx].freeEntry(cur);
            ht[ht_idx].used--;
            return true;
        }
        pre = cur;
        cur = cur->next;
    }
    return false;
}

// 判断key是否存在（同样惰性删过期key）
bool Dict::exists(const std::string& key) {
    DictEntry* e = findEntry(key);
    if (!e) {
        return false;
    }
    return true;
}

// 设置过期时间，单位秒
bool Dict::expire(const std::string& key, uint64_t expire_sec) {
    DictEntry* cur = findEntry(key);
    while (cur) {
        if (cur->key == key) {
            auto now_ms = getNow();
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
    DictEntry* e = findEntry(key);
    if (!e)
        return -2;
    uint64_t now = getNow();
    if (e->expire_ts == 0)
        return -1;
    if (now > e->expire_ts) {
        del(key);
        return -2;
    }
    return static_cast<int64_t>(e->expire_ts - now);
}

// 遍历所有有效key（给KEYS命令，简单版，无游标）
std::vector<std::string> Dict::keys() {
    std::vector<std::string> res;
    uint64_t now = getNow(); // 只获取一次当前时间
    // rehash期间 ht0、ht1 都要遍历
    for (int table_idx = 0; table_idx < 2; ++table_idx) {
        HashTable* ht = &this->ht[table_idx];
        if (ht->table == nullptr) {
            continue;
        }
        for (size_t i = 0; i < ht->size; ++i) {
            DictEntry* cur = ht->table[i];
            while (cur != nullptr) {
                // 有效：永不过期 或者 未到过期时间
                if (cur->expire_ts == 0 || cur->expire_ts > now) {
                    res.push_back(cur->key);
                }
                cur = cur->next;
            }
        }
    }
    return res;
}

// 主动尝试rehash一步；未实现渐进rehash，直接一次性rehash
void Dict::rehashStep() {
    if (rehash_idx == -1) {
        return;
    }
    DictEntry* entry = ht[0].table[rehash_idx];
    while (entry) {
        DictEntry* next = entry->next;
        size_t new_idx = hashKey(entry->key) & (ht[1].size - 1);
        entry->next = ht[1].table[new_idx];
        ht[1].table[new_idx] = entry;
        ht[1].used++;
        ht[0].used--;
        entry = next;
    }
    ht[0].table[rehash_idx] = nullptr;
    rehash_idx++;
    if ((size_t)rehash_idx >= ht[0].size) {
        ht[0].resetTable();
        ht[0] = std::move(ht[1]);
        rehash_idx = -1;
        ht_idx = 0;
        ht[1].resetTable();
    }
}

// 哈希函数
size_t Dict::hashKey(const std::string& key) {
    // 简易哈希，生产别用；只是学习
    return std::hash<std::string> { }(key);
}

// 在两张ht里查找entry
DictEntry* Dict::findEntry(const std::string& key) {
    uint64_t now = getNow();
    for (int table_idx = 0; table_idx < 2; table_idx++) {
        HashTable* ht = &this->ht[table_idx];
        if (ht->table == nullptr)
            continue;
        size_t idx = hashKey(key) & (ht->size - 1);
        DictEntry* prev = nullptr;
        DictEntry* cur = ht->table[idx];
        while (cur) {
            DictEntry* next = cur->next;
            // 核心修复：expire_ts=0 永不过期
            bool is_expired = (cur->expire_ts != 0) && (cur->expire_ts < now);

            if (is_expired) {
                // 惰性删除过期节点
                if (prev == nullptr) {
                    ht->table[idx] = next;
                } else {
                    prev->next = next;
                }
                ht->freeEntry(cur);
                cur = next;
                continue;
            }

            // 节点有效、未过期，key匹配，返回
            if (cur->key == key) {
                return cur;
            }

            // 不匹配，继续往后
            prev = cur;
            cur = next;
        }
    }
    return nullptr;
}

// 扩容触发判断
bool Dict::needExpand() {
    if (rehash_idx != -1) {
        return false;
    }
    return ht[0].used >= ht[0].size * LOAD_FACTOR;
}

// 取一个可用的 DictEntry（DictEntry 内存池的分配入口）：
// 1) 空闲链非空 -> 弹出链头节点，重置 key/val/expire_ts/next 后直接复用
//    （省掉一次 malloc/free，而且 std::string 的旧缓冲区通常能继续用）
// 2) 空闲链为空 -> 才 new 一个新节点
DictEntry* Dict::newEntry(const std::string& key, const std::string& val, uint64_t expire_ts) {
    DictEntry* e = entry_free_list;
    if (e != nullptr) {
        entry_free_list = e->next; // 弹出链头
        e->key = key;              // 赋值复用，不重新分配对象
        e->val = val;
        e->expire_ts = expire_ts;
        e->next = nullptr;         // 必须清掉它在空闲链里的 next
        return e;
    }
    return new DictEntry(key, val, expire_ts);
}

std::vector<DictEntry> Dict::getAllValidEntries() {
    std::vector<DictEntry> res;
    uint64_t now = getNow();
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
