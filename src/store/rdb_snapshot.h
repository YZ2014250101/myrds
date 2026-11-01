#pragma once
#include "dict.h"
#include <string>

class RDBSnapshot {
public:
    bool save(const std::string& path = "dump.rdb", Dict* dict = nullptr);
    bool load(const std::string& path = "dump.rdb", Dict* dict = nullptr);
};