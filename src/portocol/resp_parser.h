#pragma once
#include <string>
#include <vector>
// 一条解析好的命令
struct RespCmd {
    std::string cmd;
    std::vector<std::string> args;
    void clear() {
        cmd.clear();
        args.clear();
    }
};
// 状态
enum class RespState { READY, ARRAY_LEN, BULK_LEN, BULK_DATA, ERROR };
class RespParser {
public:
    RespParser();
    bool feed(const char* data, size_t len);
    RespCmd getCmd() const { return cmd_; }
    void reset();

private:
    RespState state_;
    RespCmd cmd_;
    int arrayRemain_;
    int bulkRemain_;
    std::string tmpBuf_;
};

// 打包函数：把结果转成RESP字符串
std::string resp_simple_string(const std::string& s);
std::string resp_error(const std::string& err);
std::string resp_integer(long long n);
std::string resp_bulk_string(const std::string& s);
std::string resp_null();
