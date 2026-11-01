#include "rdb_snapshot.h"
#include "../util/util.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
RDBSnapshot::RDBSnapshot() {
    // 初始化crc64_table
    uint64_t poly = 0xC96C5795D7870F42ULL;
    for (uint64_t i = 0; i < 256; ++i) {
        uint64_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1)
                crc = (crc >> 1) ^ poly;
            else
                crc >>= 1;
        }
        crc64_table[i] = crc;
    }
}

uint64_t RDBSnapshot::crc64(uint64_t crc, const unsigned char* buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char b = buf[i];
        crc = crc64_table[(crc ^ b) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

bool RDBSnapshot::save(const std::string& path, Dict* dict) {
    if (!dict) {
        fprintf(stderr, "[RDB] save: dict nullptr\n");
        return false;
    }
    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) {
        fprintf(stderr, "[RDB] save open %s failed\n", path.c_str());
        return false;
    }
    uint64_t crc = crc64(0, nullptr, 0);
    uint64_t now = getNow();

    // 1. 魔数 REDIS 5字节
    const char* magic = "REDIS";
    fwrite(magic, 1, 5, fp);
    crc = crc64(crc, (const unsigned char*)magic, 5);

    // 2. 版本号，转大端
    uint32_t ver_be = htobe32(RDB_VER);
    fwrite(&ver_be, sizeof(ver_be), 1, fp);
    crc = crc64(crc, (const unsigned char*)&ver_be, sizeof(ver_be));

    auto entrys = dict->getAllValidEntries();
    for (auto& e : entrys) {
        // 再次兜底过滤过期（双重保险）
        if (e.expire_ts != 0 && e.expire_ts < now)
            continue;

        std::string k = e.key;
        std::string v = e.val;
        uint32_t klen = (uint32_t)k.size();
        uint32_t vlen = (uint32_t)v.size();

        // 转大端再写入
        uint32_t klen_be = htobe32(klen);
        uint32_t vlen_be = htobe32(vlen);

        fwrite(&klen_be, sizeof(klen_be), 1, fp);
        crc = crc64(crc, (const unsigned char*)&klen_be, 4);

        fwrite(k.data(), 1, klen, fp);
        crc = crc64(crc, (const unsigned char*)k.data(), klen);

        fwrite(&vlen_be, sizeof(vlen_be), 1, fp);
        crc = crc64(crc, (const unsigned char*)&vlen_be, 4);

        fwrite(v.data(), 1, vlen, fp);
        crc = crc64(crc, (const unsigned char*)v.data(), vlen);

        uint64_t exp_be = htobe64(e.expire_ts);
        fwrite(&exp_be, sizeof(exp_be), 1, fp);
        crc = crc64(crc, (const unsigned char*)&exp_be, 8);
    }

    // 末尾写入CRC64（大端）
    uint64_t crc_be = htobe64(crc);
    fwrite(&crc_be, sizeof(crc_be), 1, fp);

    fclose(fp);
    fprintf(stderr, "[RDB] save success, key count: %zu, crc64=0x%lx\n", entrys.size(), crc);
    return true;
}

// 加载RDB快照
bool RDBSnapshot::load(const std::string& path, Dict* dict) {
    if (!dict) {
        fprintf(stderr, "[RDB] load: dict nullptr\n");
        return false;
    }
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) {
        fprintf(stderr, "[RDB] rdb file not found\n");
        return false;
    }
    uint64_t crc_calc = crc64(0, nullptr, 0);

    // 1. 读魔数
    char magic[6] = { 0 };
    if (fread(magic, 1, 5, fp) != 5) {
        fprintf(stderr, "[RDB] read magic fail\n");
        fclose(fp);
        return false;
    }
    crc_calc = crc64(crc_calc, (const unsigned char*)magic, 5);
    if (memcmp(magic, RDB_MAGIC, 5) != 0) {
        fprintf(stderr, "[RDB] bad magic, not a valid rdb file\n");
        fclose(fp);
        return false;
    }

    // 2. 读版本号
    uint32_t ver_be;
    if (fread(&ver_be, sizeof(ver_be), 1, fp) != 1) {
        fclose(fp);
        return false;
    }
    crc_calc = crc64(crc_calc, (const unsigned char*)&ver_be, sizeof(ver_be));
    uint32_t ver = be32toh(ver_be);
    if (ver != RDB_VER) {
        fprintf(stderr, "[RDB] version mismatch, file ver=%u, my ver=%u\n", ver, RDB_VER);
        fclose(fp);
        return false;
    }

    // 先拿到文件末尾位置，用来读取最后的CRC
    fseek(fp, -(int)sizeof(uint64_t), SEEK_END);
    long crc_offset = ftell(fp);
    uint64_t crc_file_be;
    fread(&crc_file_be, sizeof(crc_file_be), 1, fp);
    uint64_t crc_file = be64toh(crc_file_be);

    // 回到版本后面，开始读数据
    fseek(fp, 5 + sizeof(uint32_t), SEEK_SET);

    // 循环读每条kv，读到crc_offset为止
    while (ftell(fp) < crc_offset) {
        uint32_t klen_be;
        if (fread(&klen_be, sizeof(klen_be), 1, fp) != 1)
            break;
        crc_calc = crc64(crc_calc, (const unsigned char*)&klen_be, 4);
        uint32_t klen = be32toh(klen_be);

        std::string key(klen, 0);
        fread(&key[0], 1, klen, fp);
        crc_calc = crc64(crc_calc, (const unsigned char*)key.data(), klen);

        uint32_t vlen_be;
        if (fread(&vlen_be, sizeof(vlen_be), 1, fp) != 1)
            break;
        crc_calc = crc64(crc_calc, (const unsigned char*)&vlen_be, 4);
        uint32_t vlen = be32toh(vlen_be);

        std::string val(vlen, 0);
        fread(&val[0], 1, vlen, fp);
        crc_calc = crc64(crc_calc, (const unsigned char*)val.data(), vlen);

        uint64_t exp_be;
        if (fread(&exp_be, sizeof(exp_be), 1, fp) != 1)
            break;
        crc_calc = crc64(crc_calc, (const unsigned char*)&exp_be, 8);
        uint64_t expire_ts = be64toh(exp_be);

        dict->set(key, val, expire_ts);
    }

    // 校验CRC
    if (crc_calc != crc_file) {
        fprintf(stderr, "[RDB] CRC check failed! file=0x%lx calc=0x%lx\n", crc_file, crc_calc);
        fclose(fp);
        return false;
    }
    fprintf(stderr, "[RDB] CRC ok, load finish\n");
    fclose(fp);
    return true;
}
