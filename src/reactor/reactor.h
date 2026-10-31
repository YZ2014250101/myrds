#pragma once
#include "../portocol/resp_parser.h"
#include <functional>
#include <sys/epoll.h>
#include <vector>

static constexpr int max_buffer_len = 1024;
static constexpr int epoll_events_size = 1024;
static constexpr int connblock_size = 1024;

/**
 * @brief Connection类，用于管理网络连接的读写缓冲区和回调函数
 */
class Connection {
public:
    int fd_ { -1 }; // 文件描述符，-1表示无效的文件描述符
    char rbuffer_[max_buffer_len] { 0 }; // 读缓冲区，用于存储接收到的数据
    size_t rcount_ { 0 }; // 读缓冲区中已读取的数据长度
    char wbuffer_[max_buffer_len] { 0 }; // 写缓冲区，用于存储待发送的数据
    size_t wcount_ { 0 }; // 写缓冲区中待发送的数据长度
    size_t next_len_ = max_buffer_len; // 下次期望读取的数据长度
    RespParser parser_;
    using Callback = std::function<void(int fd, int events, void* arg)>;
    Callback cb_;
};

// 连接块
struct ConnBlock {
    std::vector<Connection> block;
    ConnBlock* next { nullptr };
    ConnBlock();
};

/**
 * @brief Reactor类，实现基于epoll的事件驱动模型
 */
class Reactor {
public:
    /**
     * @brief 构造函数，初始化epoll实例和连接块
     */
    Reactor();

    /**
     * @brief 析构函数，关闭epoll实例并释放所有连接块
     */
    ~Reactor();

    // 禁用拷贝构造和赋值操作
    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;

    /**
     * @brief 获取文件描述符对应的连接对象
     * @param fd 文件描述符
     * @return Connection* 连接对象指针
     */
    Connection* get_conn(int fd);

    /**
     * @brief 创建新的连接块
     * @return int 成功返回0，失败返回错误码
     */
    int create_connblock();

    int set_listener(int listenfd, Connection::Callback cb);

    /**
     * 事件循环函数，使用epoll实现I/O多路复用
     * 持续监听文件描述符上的事件，并根据事件类型调用相应的回调函数
     */
    void loop();

    int get_epfd_();

private:
    int epfd_ = -1;
    ConnBlock* block_header_ = nullptr;
    int blk_cnt_ = 0;
};
