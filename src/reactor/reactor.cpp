#include "reactor.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

ConnBlock::ConnBlock()
    : block(connblock_size) { }

Reactor::Reactor() {
    // 创建epoll实例，参数1表示初始大小
    epfd_ = epoll_create(1);
    if (epfd_ < 0) {
        std::cerr << "epoll_create err\n";
        return;
    }
    // 创建第一个连接块
    block_header_ = new ConnBlock();
    blk_cnt_ = 1;
}

Reactor::~Reactor() {
    // 关闭epoll实例
    if (epfd_ >= 0)
        close(epfd_);
    // 释放所有连接块
    ConnBlock* cur = block_header_;
    while (cur) {
        ConnBlock* tmp = cur;
        cur = cur->next;
        delete tmp;
    }
}

Connection* Reactor::get_conn(int fd) {
    // 计算所在的连接块索引
    int blkidx = fd / connblock_size;
    // 如果需要的块不存在，则创建新块
    while (blkidx >= blk_cnt_) {
        create_connblock();
    }
    // 定位到对应的连接块
    ConnBlock* blk = block_header_;
    for (int i = 1; i < blkidx; ++i) {
        blk = blk->next;
    }
    // 返回连接对象
    return &blk->block[fd % connblock_size];
}

int Reactor::create_connblock() {
    // 创建新连接块
    ConnBlock* newblk = new ConnBlock();
    // 找到最后一个连接块
    ConnBlock* end = block_header_;
    while (end->next)
        end = end->next;
    // 将新块添加到链表末尾
    end->next = newblk;
    blk_cnt_++;
    return 0;
}

int Reactor::set_listener(int listenfd, Connection::Callback cb) {
    Connection* conn = get_conn(listenfd);
    conn->fd_ = listenfd;
    conn->cb_ = cb;
    epoll_event ev { };
    ev.data.fd = listenfd;
    ev.events = EPOLLIN;
    epoll_ctl(epfd_, EPOLL_CTL_ADD, listenfd, &ev);
    return 0;
}

void Reactor::loop() {
    // 创建事件数组，用于存储epoll_wait返回的事件
    epoll_event events[epoll_events_size] { };
    // 无限循环，持续监听事件
    while (true) {
        // 调用epoll_wait等待事件发生，-1表示无限等待
        int nready = epoll_wait(epfd_, events, epoll_events_size, -1);
        // 如果没有事件就绪，则继续下一次循环
        if (nready <= 0)
            continue;
        // 遍历所有就绪的事件
        for (int i = 0; i < nready; i++) {
            // 获取当前事件的文件描述符
            int fd = events[i].data.fd;
            // 根据文件描述符获取连接对象
            Connection* conn = get_conn(fd);
            // 如果连接没有设置回调函数，则跳过
            if (!conn->cb_)
                continue;
            // 检查是否是可读事件
            if (events[i].events & EPOLLIN) {
                // 调用回调函数处理可读事件
                conn->cb_(fd, EPOLLIN, this);
            }
            // 检查是否是可写事件
            if (events[i].events & EPOLLOUT) {
                // 调用回调函数处理可写事件
                conn->cb_(fd, EPOLLOUT, this);
            }
        }
    }
}

int Reactor::get_epfd_() { return epfd_; }
