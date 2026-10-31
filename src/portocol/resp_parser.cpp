#include "resp_parser.h"
RespParser::RespParser()
    : state_(RespState::READY)
    , arrayRemain_(0)
    , bulkRemain_(0) { }
void RespParser::reset() {
    state_ = RespState::READY;
    arrayRemain_ = 0;
    bulkRemain_ = 0;
    tmpBuf_.clear();
    cmd_.clear();
}
bool RespParser::popCmd(RespCmd& out) {
    if (cmds_.empty())
        return false;
    out = cmds_.front();
    cmds_.erase(cmds_.begin());
    return true;
}
bool RespParser::feed(const char* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        char ch = data[i];
        tmpBuf_.push_back(ch);
        if (tmpBuf_.size() > 1 && tmpBuf_[tmpBuf_.size() - 2] == '\r' && tmpBuf_[tmpBuf_.size() - 1] == '\n') {
            std::string line(tmpBuf_.begin(), tmpBuf_.end() - 2);
            tmpBuf_.clear();
            if (state_ == RespState::READY) {
                if (line[0] == '*') {
                    state_ = RespState::ARRAY_LEN;
                    cmd_.clear();
                    arrayRemain_ = std::stoi(line.substr(1));
                } else {
                    state_ = RespState::ERROR;
                    return false;
                }
            } else if (state_ == RespState::ARRAY_LEN) {
                if (line[0] == '$') {
                    state_ = RespState::BULK_LEN;
                    bulkRemain_ = std::stoi(line.substr(1));
                } else {
                    state_ = RespState::ERROR;
                    return false;
                }
            } else if (state_ == RespState::BULK_LEN) {
                if (cmd_.cmd.empty()) {
                    cmd_.cmd = line;
                } else {
                    cmd_.args.push_back(line);
                }
                arrayRemain_--;
                if (arrayRemain_ <= 0) {
                    state_ = RespState::READY;
                    cmds_.push_back(cmd_); // 成品丢进vector，继续循环解析剩下字节
                    cmd_.clear();
                }
                state_ = RespState::ARRAY_LEN;
            } else if (state_ == RespState::ERROR) {
                // 错误状态，直接跳过，等待下一个*
                if (line[0] == '*') {
                    state_ = RespState::ARRAY_LEN;
                    cmd_.clear();
                    arrayRemain_ = std::atoi(line.c_str());
                }
            }
        }
    }
    return !cmds_.empty();
}

std::string resp_simple_string(const std::string& s) { return "+" + s + "\r\n"; }
std::string resp_error(const std::string& err) { return "-ERR " + err + "\r\n"; }
std::string resp_integer(long long n) { return ":" + std::to_string(n) + "\r\n"; }
std::string resp_bulk_string(const std::string& s) { return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n"; }
std::string resp_null() { return "$-1\r\n"; }