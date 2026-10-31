#ifndef KVSTORE_H
#define KVSTORE_H

#include "../portocol/resp_parser.h"
#include "dict.h"
#include "skiplist.h"
#include <string.h>
#include <string>
#include <vector>


#define max_tokens 32
enum CmdType { SET, GET, DEL, EXISTS, KEYS, EXPIRE, TTL, UNKNOWN };

class KVStore {
public:
    KVStore();
    ~KVStore();

    // 设置键值对
    bool dictSet(const std::string& key, const std::string& value, uint64_t expire_at_ms = 0);

    // 获取键值对
    bool dictGet(const std::string& key, std::string& out_val);

    // 删除键值对
    bool dictDel(const std::string& key);

    // 判断键是否存在
    bool dictExists(const std::string& key);

    // 设置键的过期时间
    bool dictExpire(const std::string& key, int64_t seconds);

    // 获取键的过期时间
    int64_t dictTtl(const std::string& key);

    // 获取全部键
    std::vector<std::string> dictKeys();

    // 初始化，传入跳表最大层数（比如16）
    int init(int max_level);

    // SET key value
    int set(const std::string& key, const std::string& value);

    // GET key，找到返回true，val带出结果；没找到返回false
    bool get(const std::string& key, std::string& out_val);

    // DEL key
    int del(const std::string& key);

    // EXISTS key
    bool exists(const std::string& key);

    // 返回总kv数量
    int size() const;

    // 打印全部（调试用）
    void print() const;

    // 解析命令
    int parseCmd(const std::vector<std::string> token, int len);

    int splitTokens(std::vector<std::string>& tokens, const std::string& msg);

    std::string protocal(const char* buffer, size_t max_buffer_len);

    // 启动加载aof.log，回放命令
    bool loadAof();

    // 追加一条写命令到AOF
    void appendAof(const std::string& raw_resp_cmd);

    bool saveRdb(const std::string &path = "dump.rdb");
    bool loadRdb(const std::string& path = "dump.rdb");
    
    std::string CmdToRawString(const RespCmd& cmd);

private:
    Skiplist _sl;
    Dict dict_;
    RespParser AofParser_;
    int aof_fd_ { -1 };
    bool aof_enable_ { true };
};

#endif
