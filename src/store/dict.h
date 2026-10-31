#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 哈希表节点（链式）
struct DictEntry {
    std::string key;
    std::string val;
    uint64_t expire_ts; // 过期时间戳，毫秒；0=永不过期
    DictEntry* next;

    DictEntry(std::string k, std::string v, uint64_t exp)
        : key(std::move(k))
        , val(std::move(v))
        , expire_ts(exp)
        , next(nullptr) { }
};

// 哈希表一张表（Redis dict 有 ht[0], ht[1] 两张用于rehash）
struct HashTable {
    DictEntry** table;
    size_t size; // 桶数量
    size_t used; // 当前元素个数

    HashTable();
    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;

    HashTable(HashTable&& other) noexcept {
        table = other.table;
        size = other.size;
        used = other.used;
        other.table = nullptr;
        other.size = 0;
        other.used = 0;
    }
    HashTable& operator=(HashTable&& other) noexcept {
        if (this == &other)
            return *this;
        clear(); // 先释放自己持有的资源
        table = other.table;
        size = other.size;
        used = other.used;
        other.table = nullptr;
        other.size = 0;
        other.used = 0;
        return *this;
    }
    ~HashTable();

    // 初始化桶，size必须是2的幂
    bool init(size_t sz);
    void freeEntry(DictEntry* e);
    void resetTable();
    void clear();
};

class Dict {
public:
    Dict();
    ~Dict();

    // 设置键值对，expire_ms：0代表不过期
    void set(const std::string& key, const std::string& value, uint64_t expire_ms = 0);

    // 获取，返回true表示存在；同时惰性删除：如果已经过期，直接删、返回false
    bool get(const std::string& key, std::string& out_val);

    // 删除key
    bool del(const std::string& key);

    // 判断key是否存在（同样惰性删过期key）
    bool exists(const std::string& key);

    // 设置过期时间，单位秒
    bool expire(const std::string& key, uint64_t expire_sec);

    // 获取key剩余ttl毫秒；返回-1永不过期，-2不存在/已过期
    int64_t ttl(const std::string& key);

    // 遍历所有key（给KEYS命令用，简单版，不做scan游标）
    std::vector<std::string> keys();

    // 主动尝试rehash一步；你第一阶段可以先不调用
    void rehashStep();

    // 在一张ht里查找entry
    DictEntry* findEntry(HashTable* ht, const std::string& key);

    HashTable* getHashTable(int idx) { return &ht[idx]; }

private:
    // 哈希函数
    size_t hashKey(const std::string& key);

    // 扩容触发判断
    bool needExpand();

private:
    HashTable ht[2];
    int rehash_idx; // -1 代表不在rehash；>=0 代表正在rehash，当前处理桶下标
    static constexpr size_t INIT_SIZE = 4;
    static constexpr double LOAD_FACTOR = 0.75;
};
