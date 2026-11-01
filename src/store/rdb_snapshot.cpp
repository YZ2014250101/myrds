#include "rdb_snapshot.h"
#include "../util/util.h"
bool RDBSnapshot::save(const std::string& path, Dict* dict) {
    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) {
        fprintf(stderr, "[RDB] save open %s failed\n", path.c_str());
        return false;
    }

    auto keys = dict->keys();
    uint64_t now = getNow();
    auto entrys = dict->getAllValidEntries();
    for (auto& e : entrys) {
        if (e.expire_ts != 0 && e.expire_ts < now)
            continue;
        std::string k = e.key;
        std::string v = e.val;

        // 写入格式：
        // key_len(uint32) + key_data
        // val_len(uint32) + val_data
        // expire_ts(uint64)
        uint32_t klen = (uint32_t)k.size();
        uint32_t vlen = (uint32_t)v.size();
        fwrite(&klen, sizeof(klen), 1, fp);
        fwrite(k.data(), 1, klen, fp);
        fwrite(&vlen, sizeof(vlen), 1, fp);
        fwrite(v.data(), 1, vlen, fp);
        fwrite(&e.expire_ts, sizeof(uint64_t), 1, fp);
    }

    fclose(fp);
    fprintf(stderr, "[RDB] save success, key count: %zu\n", keys.size());
    return true;
}

// 加载RDB快照
bool RDBSnapshot::load(const std::string& path, Dict* dict) {
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

        dict->set(key, val, expire_ts);
        cnt++;
    }
    fclose(fp);
    fprintf(stderr, "[RDB] load finish, loaded %zu keys\n", cnt);
    return true;
}
