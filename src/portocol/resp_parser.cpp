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
                    return true;
                }
                state_ = RespState::ARRAY_LEN;
            }
        }
    }
}

std::string resp_simple_string(const std::string& s) { return "+" + s + "\r\n"; }
std::string resp_error(const std::string& err) { return "-ERR " + err + "\r\n"; }
std::string resp_integer(long long n) { return ":" + std::to_string(n) + "\r\n"; }
std::string resp_bulk_string(const std::string& s) { return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n"; }
std::string resp_null() { return "$-1\r\n"; }