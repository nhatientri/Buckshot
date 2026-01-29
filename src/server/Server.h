#include <vector>
#include <string>
#include <map>
#include <memory>
#include "UserManager.h"
#include "GameSession.h"
#include "SocketServer.h"
#include <chrono>

namespace Buckshot {

class Server {
public:
    explicit Server(int port);
    void run();

private:
    int port;
    bool running;
    
    SocketServer socketServer;
    
    // Connected clients (socket FD -> buffer)
    std::map<int, std::vector<char>> clientBuffers;

    UserManager userManager;
    
    // session state
    std::map<int, std::string> authenticatedUsers; 
    std::vector<std::shared_ptr<GameSession>> activeGames;
    std::chrono::steady_clock::time_point lastTimeoutCheck;
    std::chrono::steady_clock::time_point lastMatchmakingBatch;
    std::chrono::steady_clock::time_point lastStateBroadcast;

    void onConnect(int clientFd);
    void onData(int clientFd, const char* data, size_t size);
    void onDisconnect(int clientFd);
    
    void startGameloop();
    
    void broadcastUserList();
    void processPacket(int client, PacketHeader& header, const std::vector<char>& body);
    
    std::shared_ptr<GameSession> getGameSession(int client);
    
    std::map<std::string, std::string> pendingChallenges; // Challenger -> Target
    std::vector<std::string> matchmakingQueue; // Users waiting for match (Username)
    void processMatchmaking();
    
    // Security
    std::map<std::string, int> failedLoginAttempts; // IP -> Count
    std::map<std::string, std::chrono::steady_clock::time_point> ipLockout; // IP -> UnlockTime
    
    // Helper to find socket by username
    int getSocketByUsername(const std::string& username);
    
    // Helper to send using SocketServer
    void sendPacket(int client, const void* data, size_t size);

    /* [ASIO REFERENCE]
    // Asio
    asio::io_context io_context;
    asio::ip::tcp::acceptor acceptor;
    asio::steady_timer gameloop_timer;
    std::vector<std::shared_ptr<asio::ip::tcp::socket>> clients;

    void doAccept();
    void doRead(std::shared_ptr<asio::ip::tcp::socket> socket);
    
    // std::map<std::shared_ptr<asio::ip::tcp::socket>, std::string> authenticatedUsers;
    */
};

}
