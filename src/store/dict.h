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
        // 内存池指针是借用的，不是这张表拥有的资源：
        // 搬过来的同时保留源表的值，让 ht[0]/ht[1] 始终共享 Dict 的同一条空闲链
        free_list_head_ = other.free_list_head_;
        other.table = nullptr;
        other.size = 0;
        other.used = 0;
    }
    HashTable& operator=(HashTable&& other) noexcept {
        if (this == &other)
            return *this;
        clear(); // 先释放自己持有的资源（节点会挂回内存池，不是 delete）
        table = other.table;
        size = other.size;
        used = other.used;
        free_list_head_ = other.free_list_head_; // 同上：共享，不夺走
        other.table = nullptr;
        other.size = 0;
        other.used = 0;
        return *this;
    }
    ~HashTable();

    // 初始化桶，size必须是2的幂
    bool init(size_t sz);
    // 释放一个节点：有内存池时挂回 Dict 的 DictEntry 空闲链（复用），
    // 没有池时才真正 delete。名字和签名保持原样，调用点不用改。
    void freeEntry(DictEntry* e);
    void resetTable();
    void clear();

private:
    friend class Dict; // 只有 Dict 能挂接 / 回收内存池

    // 指向 Dict::entry_free_list 这个成员本身（“头指针的指针”），
    // 这样 freeEntry 能直接头插，不用把新头回传给 Dict。
    // nullptr 表示这张表没挂接内存池（独立使用的 HashTable），此时 freeEntry 退回 delete。
    DictEntry** free_list_head_ { nullptr };
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
    DictEntry* findEntry(const std::string& key);

    HashTable* getHashTable(int idx) { return &ht[idx]; }

    std::vector<DictEntry> getAllValidEntries();

private:
    // 哈希函数
    size_t hashKey(const std::string& key);

    // 扩容触发判断
    bool needExpand();

    // 取一个可用的 DictEntry：优先从 entry_free_list 弹出现成节点复用，
    // 空闲链为空才 new 一个新节点
    DictEntry* newEntry(const std::string& key, const std::string& val, uint64_t expire_ts);

private:
    // DictEntry 空闲链表内存池（对象池）：freeEntry 回收的节点头插挂在这里，
    // 直接复用节点自带的 next 字段串链，零额外管理内存
    DictEntry* entry_free_list = nullptr;

    HashTable ht[2];
    int rehash_idx = -1; // -1 代表不在rehash；>=0 代表正在rehash，当前处理桶下标
    int ht_idx = 0; // 当前使用的ht下标，0或1
    static constexpr size_t INIT_SIZE = 4;
    static constexpr double LOAD_FACTOR = 1;
};
