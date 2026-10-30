#include "tcp/tcp_server.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout << "./server port\n";
        return -1;
    }
    int port = atoi(argv[1]);
    TcpServer server(port);
    server.start();
    return 0;
}
