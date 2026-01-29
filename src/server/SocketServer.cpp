#include "SocketServer.h"
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <algorithm>

#ifdef __linux__
#include <sys/epoll.h>
#else
// Stub for non-Linux to allow compilation (will fail at runtime)
#include <sys/types.h>
#define EPOLLIN 0x001
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define epoll_create1(x) -1
#define epoll_ctl(a,b,c,d) -1
#define epoll_wait(a,b,c,d) -1
struct epoll_event {
    uint32_t events;
    union {
        void *ptr;
        int fd;
        uint32_t u32;
        uint64_t u64;
    } data;
};
#endif

#define MAX_EVENTS 1024
#define BUFFER_SIZE 4096

namespace Buckshot {

SocketServer::SocketServer(int port) : port(port), running(false), serverFd(-1), epollFd(-1) {
}

SocketServer::~SocketServer() {
    stop();
}

void SocketServer::setupServer() {
#ifndef __linux__
    std::cerr << "CRITICAL ERROR: Epoll is only supported on Linux! This server will not run on macOS." << std::endl;
    exit(1);
#endif

    // 1. Create Server Socket
    serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        perror("socket failed");
        exit(1);
    }

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(serverFd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(1);
    }

    // Increased backlog to SOMAXCONN for high concurrency (C10k+)
    if (listen(serverFd, SOMAXCONN) < 0) {
        perror("listen failed");
        exit(1);
    }

    // 2. Create Epoll
    epollFd = epoll_create1(0);
    if (epollFd < 0) {
        perror("epoll_create1 failed");
        exit(1);
    }

    // 3. Add Server Socket to Epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = serverFd;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, serverFd, &ev) == -1) {
        perror("epoll_ctl: serverFd");
        exit(1);
    }
    
    std::cout << "Server listening on port " << port << " (Epoll Mode)" << std::endl;
}

void SocketServer::run() {
    setupServer();
    running = true;

    struct epoll_event events[MAX_EVENTS];
    char buffer[BUFFER_SIZE];

    while (running) {
        // Wait for events (timeout 10ms to allow timer processing)
        int nfds = epoll_wait(epollFd, events, MAX_EVENTS, 10);

        if (nfds == -1) {
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == serverFd) {
                // New Connection
                struct sockaddr_in clientAddr;
                socklen_t clientLen = sizeof(clientAddr);
                int clientFd = accept(serverFd, (struct sockaddr*)&clientAddr, &clientLen);
                if (clientFd == -1) {
                    perror("accept");
                    continue;
                }

                setNonBlocking(clientFd);

                struct epoll_event ev;
                ev.events = EPOLLIN; // Watch for Input
                ev.data.fd = clientFd;
                if (epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &ev) == -1) {
                    perror("epoll_ctl: clientFd");
                    close(clientFd);
                } else {
                    if (onConnect) onConnect(clientFd);
                }

            } else {
                // Client Data
                int clientFd = events[i].data.fd;
                int bytesRead = read(clientFd, buffer, BUFFER_SIZE);

                if (bytesRead > 0) {
                    if (onData) onData(clientFd, buffer, bytesRead);
                } else {
                    // Closed or Error
                    if (onDisconnect) onDisconnect(clientFd);
                    close(clientFd);
                    epoll_ctl(epollFd, EPOLL_CTL_DEL, clientFd, nullptr);
                }
            }
        }
        
        processTimers();
    }
}

void SocketServer::processTimers() {
    auto now = std::chrono::steady_clock::now();
    for (auto& timer : timers) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - timer.lastRun).count();
        if (elapsed >= timer.intervalMs) {
            if (timer.callback) timer.callback();
            timer.lastRun = now;
        }
    }
}

void SocketServer::stop() {
    running = false;
    if (serverFd >= 0) close(serverFd);
    if (epollFd >= 0) close(epollFd);
}

void SocketServer::sendData(int socket, const void* data, size_t size) {
    // Send directly (Should handle EAGAIN in a real robust server, but blocking send is okay for simple logic)
    send(socket, data, size, 0); 
}

void SocketServer::closeSocket(int socket) {
    if (epollFd >= 0) epoll_ctl(epollFd, EPOLL_CTL_DEL, socket, nullptr);
    close(socket);
}

int SocketServer::addTimer(int intervalMs, std::function<void()> callback) {
    Timer t;
    t.id = nextTimerId++;
    t.intervalMs = intervalMs;
    t.callback = callback;
    t.lastRun = std::chrono::steady_clock::now();
    timers.push_back(t);
    return t.id;
}

void SocketServer::removeTimer(int timerId) {
    timers.erase(std::remove_if(timers.begin(), timers.end(), 
        [timerId](const Timer& t){ return t.id == timerId; }), timers.end());
}

void SocketServer::setNonBlocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

}
