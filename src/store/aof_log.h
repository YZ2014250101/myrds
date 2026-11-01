#pragma once
#include "dict.h"
#include <atomic>
#include <signal.h>
#include <string>
#include <unistd.h>
#include <vector>
enum RewriteState {
    REWRITE_NONE, // 无重写
    REWRITE_IN_PROGRESS // 正在后台AOF重写
};

enum class RewriteRet {
    OK,
    BUSY, // 正在重写
    FORK_ERR, // fork失败
};

struct RewriteResult {
    RewriteRet ret;
    std::string msg;
};

class Reactor;
class KVStore;

class AofLog {
public:
    AofLog(const std::string& aof_file, Reactor* reactor, KVStore* kv);
    ~AofLog();
    RewriteResult bgRewrite(Dict& dict); // 传入dict引用，读取快照
    void finish_rewrite(); // 重写完成
    void append(const std::string& cmd); // 写入aof文件
    void load(); // 回放aof文件
private:
    static void sigchld_sa_handler(int sig, siginfo_t* info, void* uctx); // 子进程结束信号处理函数
    static AofLog* s_instance_; // 静态实例指针
    int sig_pipe_[2];

    int aof_fd_ { -1 };
    bool aof_enable_ { true };

    KVStore* kv_;
    Reactor* reactor_;

    RewriteState rewrite_state_ = REWRITE_NONE;
    pid_t rewrite_child_pid_ = -1;
    std::vector<std::string> rewrite_buffer_;
    std::atomic<bool> child_rewrite_done_flag_ { false };

    std::string aof_path_;
    std::string tmp_path_;

    void doRewriteWork(Dict& dict); // 子进程进行重写，传入dict引用，写入aof文件
};