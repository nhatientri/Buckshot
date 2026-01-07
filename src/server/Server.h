#pragma once
#include <vector>
#include <string>
#include <map>
#include <memory>
#include "UserManager.h"
#include "GameSession.h"
#include <chrono>

namespace Buckshot {

class Server {
public:
    explicit Server(int port);
    void run();

private:
    int port;
    int serverSocket;
    bool running;
    std::vector<int> clientSockets;
    UserManager userManager;
    
    // session state
    std::map<int, std::string> authenticatedUsers; // fd -> username
    std::vector<std::shared_ptr<GameSession>> activeGames;
    std::chrono::steady_clock::time_point lastTimeoutCheck;

    void setupSocket();
    void handleNewConnection();
    void handleClientActivity(fd_set& readfds);
    void processPacket(int clientFd, const char* buffer, int size);
    
    std::shared_ptr<GameSession> getGameSession(int fd);
};

}
