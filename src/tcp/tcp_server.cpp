#include "tcp_server.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
TcpServer::TcpServer(int port)
    : port_(port) { }

void TcpServer::start() {
    int listenfd = init_listen(); // 初始化监听socket
    kvstore_.init(16); // 初始化KV存储
    // 设置连接接受回调函数
    reactor_.set_listener(listenfd, [this](int fd, int event, void* arg) { this->accept_cb(fd, event, arg); });
    std::cout << "init finish, listening...\n";
    reactor_.loop(); // 进入事件循环
}

int TcpServer::init_listen() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket create failed");
        return -1;
    }
    // 端口复用，解决 TIME_WAIT 无法快速重启
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        close(sockfd);
        return -1;
    }
    sockaddr_in servaddr { }; // 服务器地址结构
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); // 监听所有网络接口
    servaddr.sin_port = htons(port_); // 设置端口
    int n = bind(sockfd, (sockaddr*)&servaddr, sizeof(servaddr)); // 绑定地址
    if (n < 0) {
        perror("bind failed");
        close(sockfd);
        return -1;
    }
    n = listen(sockfd, 10);
    if (n < 0) {
        perror("listen failed");
        close(sockfd);
        return -1;
    }
    std::cout << "listen port:" << port_ << " sockfd:" << sockfd << "\n";
    return sockfd;
}

void TcpServer::accept_cb(int fd, int event, void* arg) {
    sockaddr_in cli { };
    socklen_t len = sizeof(cli);
    int clientfd = accept(fd, (sockaddr*)&cli, &len);
    if (clientfd < 0)
        return;
    Reactor* r = (Reactor*)arg;
    Connection* conn = r->get_conn(clientfd);
    conn->fd_ = clientfd;
    conn->rcount_ = 0;
    conn->wcount_ = 0;
    conn->next_len_ = max_buffer_len;
    conn->cb_ = [this](int fd, int event, void* arg) { this->recv_cb(fd, event, arg); };
    epoll_event ev { };
    ev.data.fd = clientfd;
    ev.events = EPOLLIN;
    epoll_ctl(r->get_epfd_(), EPOLL_CTL_ADD, clientfd, &ev);
    std::cout << "client connect:" << clientfd << "\n";
}

void TcpServer::recv_cb(int fd, int event, void* arg) {
    Reactor* r = (Reactor*)arg;
    Connection* conn = r->get_conn(fd);
    if (conn == nullptr) {
        return;
    }
    int n = recv(fd, conn->rbuffer_ + conn->rcount_, conn->next_len_, 0);
    if (n <= 0) {
        epoll_ctl(r->get_epfd_(), EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
        return;
    }
    conn->rcount_ += n;
    std::cerr << "[recv] n=" << n << ", rbuffer: " << conn->rbuffer_ << std::endl;
    bool parse_ok = conn->parser_.feed(conn->rbuffer_ + conn->rcount_ - n, n);
    std::cerr << "[parse_ok] = " << parse_ok << std::endl;
    if (parse_ok) {
        RespCmd cmd;
        conn->parser_.popCmd(cmd);
        std::cerr << "cmd:" << cmd.cmd << std::endl;
        // 命令统一转小写，兼容 get / GET
        std::transform(cmd.cmd.begin(), cmd.cmd.end(), cmd.cmd.begin(), ::tolower);

        std::string raw_cmd_str = CmdToRawString(cmd);
        // 调用你原来的 protocal，参数就是拼接好的字符串 + 长度
        std::string res = kvstore_.protocal(raw_cmd_str.c_str(), raw_cmd_str.size());

        std::cerr << "final resp send:" << res << std::endl;
        // 重置parser，准备下一条命令
        conn->parser_.reset();

        // 清空读缓冲区
        conn->rcount_ = 0;
        memset(conn->rbuffer_, 0, max_buffer_len);

        // 防止发送缓冲区溢出
        if (res.size() >= max_buffer_len) {
            res = "-ERR reply too long\r\n";
        }
        memcpy(conn->wbuffer_, res.c_str(), res.size());
        conn->wcount_ = res.size();

        // 切换成写事件，触发send_cb
        conn->cb_ = [this](int fd, int event, void* arg) { this->send_cb(fd, event, arg); };
        epoll_event ev { };
        ev.data.fd = fd;
        ev.events = EPOLLOUT;
        epoll_ctl(r->get_epfd_(), EPOLL_CTL_MOD, fd, &ev);
    }
}

void TcpServer::send_cb(int fd, int event, void* arg) {
    Reactor* r = (Reactor*)arg;
    Connection* conn = r->get_conn(fd);
    int n = send(fd, conn->wbuffer_, conn->wcount_, 0);
    if (n < 0) {
        // 非阻塞缓冲区满，暂时发不出，保留EPOLLOUT，下次继续发
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        // 真正错误，关闭连接
        epoll_ctl(r->get_epfd_(), EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
        return;
    }
    conn->wcount_ -= n;
    memset(conn->wbuffer_, 0, max_buffer_len);
    // 切回读
    conn->cb_ = [this](int fd, int event, void* arg) { this->recv_cb(fd, event, arg); };
    epoll_event ev { };
    ev.data.fd = fd;
    ev.events = EPOLLIN;
    epoll_ctl(r->get_epfd_(), EPOLL_CTL_MOD, fd, &ev);
}

std::string TcpServer::CmdToRawString(const RespCmd& cmd) {
    std::string out = cmd.cmd;
    for (auto& arg : cmd.args) {
        out += " ";
        out += arg;
    }
    return out;
}