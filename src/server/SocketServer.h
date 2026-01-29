#pragma once
#include <vector>
#include <string>
#include <functional>
#include <map>
#include <chrono> // For Timers

namespace Buckshot {

// Forward declaration
class GameSession;

// Callback types
// OnConnect(socket_fd)
using ConnectCallback = std::function<void(int)>;
// OnData(socket_fd, buffer, size)
using DataCallback = std::function<void(int, const char*, size_t)>;
// OnDisconnect(socket_fd)
using DisconnectCallback = std::function<void(int)>;

class SocketServer {
public:
    SocketServer(int port);
    ~SocketServer();

    void run();
    void stop();

    // Setters for callbacks
    void setConnectCallback(ConnectCallback cb) { onConnect = cb; }
    void setDataCallback(DataCallback cb) { onData = cb; }
    void setDisconnectCallback(DisconnectCallback cb) { onDisconnect = cb; }

    // Timer (to replace asio::steady_timer)
    // Returns a timer ID
    int addTimer(int intervalMs, std::function<void()> callback);
    void removeTimer(int timerId);

    // Helpers
    void sendData(int socket, const void* data, size_t size);
    void closeSocket(int socket);

private:
    int port;
    bool running;
    int serverFd;
    int epollFd;

    ConnectCallback onConnect;
    DataCallback onData;
    DisconnectCallback onDisconnect;

    // Timer Structure
    struct Timer {
        int id;
        int intervalMs;
        std::chrono::steady_clock::time_point lastRun;
        std::function<void()> callback;
    };
    std::vector<Timer> timers;
    int nextTimerId = 1;

    void setupServer();
    void processTimers();
    
    // Non-blocking helper
    void setNonBlocking(int sock);
};

}
