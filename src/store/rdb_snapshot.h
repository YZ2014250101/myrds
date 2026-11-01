#pragma once
#include "dict.h"
#include <string>

class RDBSnapshot {
public:
    RDBSnapshot();
    bool save(const std::string& path = "dump.rdb", Dict* dict = nullptr);
    bool load(const std::string& path = "dump.rdb", Dict* dict = nullptr);

private:
    // 魔数 REDIS 5个字节
    uint64_t crc64_table[256] { 0 }; // CRC64校验码
    uint64_t crc64(uint64_t crc, const unsigned char* buf, size_t len);
    static constexpr const char* RDB_MAGIC = "REDIS";
    static constexpr uint32_t RDB_VER = 1;
};