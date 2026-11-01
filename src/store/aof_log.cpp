#include "aof_log.h"
#include "../reactor/reactor.h"
#include "../store/kv_store.h"
#include <../util/util.h>
#include <fcntl.h>
#include <fstream>
#include <signal.h>
#include <sys/wait.h>

AofLog* AofLog::s_instance_ = nullptr;

void AofLog::sigchld_sa_handler(int sig, siginfo_t* info, void* uctx) {
    (void)sig;
    (void)uctx;
    if (!AofLog::s_instance_)
        return;
    pid_t pid = info->si_pid;
    if (pid == AofLog::s_instance_->rewrite_child_pid_) {
        char ch = 1;
        write(AofLog::s_instance_->sig_pipe_[1], &ch, 1);
    }
}

// aof_log.cpp
AofLog::AofLog(const std::string& aof_file, Reactor* reactor, KVStore* kv)
    : kv_(kv)
    , reactor_(reactor)
    , rewrite_state_(RewriteState::REWRITE_NONE)
    , rewrite_child_pid_(-1)
    , aof_path_(aof_file)
    , tmp_path_(aof_file + ".tmp") {
    // 1. 创建管道
    if (pipe(sig_pipe_) < 0) {
        perror("pipe create fail");
        abort();
    }
    // 2. 设置非阻塞（非常关键）
    setNonblock(sig_pipe_[0]);
    setNonblock(sig_pipe_[1]);

    // ========== 这里就是绑定 sig_pipe_[0] 到epoll ==========
    auto pipe_cb = [this](int fd, int events, void* arg) {
        (void)fd;
        (void)events;
        (void)arg;
        char buf[64];
        // 循环读空管道，避免一直触发EPOLLIN
        while (read(sig_pipe_[0], buf, sizeof(buf)) > 0)
            ;

        int status = 0;
        while (waitpid(-1, &status, WNOHANG) > 0) {
            if (WIFEXITED(status)) {
                int code = WEXITSTATUS(status);
                if (code == 0) {
                    finish_rewrite();
                } else {
                    rewrite_state_ = RewriteState::REWRITE_NONE;
                    rewrite_child_pid_ = -1;
                    unlink(tmp_path_.c_str());
                }
            }
        }
    };
    reactor_->set_listener(sig_pipe_[0], pipe_cb);

    s_instance_ = this;
    struct sigaction sa { };
    sa.sa_sigaction = sigchld_sa_handler;
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGCHLD, &sa, nullptr) < 0) {
        perror("sigaction");
        abort();
    }
}

AofLog::~AofLog() {
    close(sig_pipe_[0]);
    close(sig_pipe_[1]);
}

RewriteResult AofLog::bgRewrite(Dict& dict) {
    if (rewrite_state_ == REWRITE_IN_PROGRESS) {
        return { RewriteRet::BUSY, "rewrite in progress" };
    }
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork fail");
        return { RewriteRet::FORK_ERR, "fork() failed" };
    }
    if (pid == 0) {
        close(sig_pipe_[0]);
        close(sig_pipe_[1]);
        doRewriteWork(dict);
    }
    rewrite_child_pid_ = pid;
    rewrite_state_ = REWRITE_IN_PROGRESS;
    return { RewriteRet::OK, "Background append only file rewrite started" };
}

void AofLog::doRewriteWork(Dict& dict) {
    // TODO: implement

    FILE* fp = fopen(tmp_path_.c_str(), "w");
    if (!fp) {
        perror("[Child] fopen aof.log.tmp failed");
        _exit(1);
    }
    uint64_t now = getNow();
    auto entries = dict.getAllValidEntries();
    for (auto& entry : entries) {
        fprintf(fp, "SET %s %s\n", entry.key.c_str(), entry.val.c_str());
        if (entry.expire_ts != 0) {
            int64_t remain = (entry.expire_ts - now) / 1000;
            if (remain > 0) {
                fprintf(fp, "EXPIRE %s %ld\n", entry.key.c_str(), remain);
            }
        }
    }
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
    _exit(0);
}

void AofLog::finish_rewrite() {
    FILE* fr = fopen(tmp_path_.c_str(), "r");
    if (!fr) {
        perror("aof.log.tmp not found, child rewrite failed");
        rewrite_state_ = REWRITE_NONE;
        rewrite_child_pid_ = -1;
        rewrite_buffer_.clear();
        return;
    }
    fclose(fr);

    FILE* fp = fopen(tmp_path_.c_str(), "a");
    if (!fp) {
        rewrite_state_ = REWRITE_NONE;
        rewrite_child_pid_ = -1;
        rewrite_buffer_.clear();
        return;
    }

    for (auto& cmd : rewrite_buffer_) {
        fprintf(fp, "%s\n", cmd.c_str());
    }

    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);

    if (rename(tmp_path_.c_str(), "aof.log") < 0) {
        perror("rename aof.log.tmp -> aof.log failed");
        unlink(tmp_path_.c_str());
    }

    rewrite_buffer_.clear();
    rewrite_state_ = REWRITE_NONE;
    rewrite_child_pid_ = -1;
}

void AofLog::append(const std::string& cmd) {
    if (!aof_enable_ || aof_fd_ < 0)
        return;
    FILE* fp = fopen("aof.log", "a");
    if (fp) {
        fprintf(fp, "%s\n", cmd.c_str());
        fflush(fp);
        fclose(fp);
    }

    if (rewrite_state_ == REWRITE_IN_PROGRESS) {
        rewrite_buffer_.push_back(cmd);
    }
}

void AofLog::load() {
    if (!aof_enable_) {
        return;
    }

    std::ifstream fin("aof.log");
    std::string line;

    if (fin.is_open()) {
        while (std::getline(fin, line)) {
            if (line.empty()) {
                continue;
            }
            kv_->protocal(line.data(), line.size());
        }

        fin.close();
        fprintf(stderr, "[AOF] load aof done\n");
    } else {
        fprintf(stderr, "[AOF] aof.log not exist, skip load\n");
    }

    aof_fd_ = open("aof.log", O_WRONLY | O_APPEND | O_CREAT, 0644);

    if (aof_fd_ < 0) {
        fprintf(stderr, "[AOF] open aof write fail: %s\n", strerror(errno));
        aof_enable_ = false;
        return;
    }
}