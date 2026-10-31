#pragma once
#include "../reactor/reactor.h"
#include "../store/kv_store.h"

/**
 * TcpServer类 - 实现一个基于Reactor模式的TCP服务器
 * 支持监听指定端口，处理客户端连接，对接KV存储
 */
class TcpServer {
public:
    /**
     * 构造函数
     * @param port 服务器监听端口
     */
    TcpServer(int port);

    /**
     * 启动服务器
     * 初始化监听socket，设置回调，进入事件循环
     */
    void start();

private:
    int port_; // 服务器监听端口
    Reactor reactor_; // Reactor事件循环对象
    KVStore kvstore_; // KV存储对象

    /**
     * 初始化监听socket
     * @return 返回监听socket的文件描述符，失败返回-1
     */
    int init_listen();

    void accept_cb(int fd, int event, void* arg);
    void recv_cb(int fd, int event, void* arg);
    void send_cb(int fd, int event, void* arg);
    std::string CmdToRawString(const RespCmd& cmd);
};
