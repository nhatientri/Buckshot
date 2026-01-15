#define ASIO_STANDALONE
#include <asio.hpp>
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
    bool running;
    
    // Asio
    asio::io_context io_context;
    asio::ip::tcp::acceptor acceptor;
    asio::steady_timer gameloop_timer;
    std::vector<std::shared_ptr<asio::ip::tcp::socket>> clients;

    UserManager userManager;
    
    // session state
    // We will use the raw socket pointer address as a unique ID for now, or maintain a map
    // For simplicity in migration, let's map shared_ptr<socket> to user data
    std::map<std::shared_ptr<asio::ip::tcp::socket>, std::string> authenticatedUsers; 
    std::vector<std::shared_ptr<GameSession>> activeGames;
    std::chrono::steady_clock::time_point lastTimeoutCheck;
    std::chrono::steady_clock::time_point lastMatchmakingBatch;
    std::chrono::steady_clock::time_point lastStateBroadcast;

    void doAccept();
    void doRead(std::shared_ptr<asio::ip::tcp::socket> socket);
    void startGameloop();
    
    void broadcastUserList();
    void processPacket(std::shared_ptr<asio::ip::tcp::socket> client, PacketHeader& header, const std::vector<char>& body);
    
    std::shared_ptr<GameSession> getGameSession(std::shared_ptr<asio::ip::tcp::socket> client);
    
    std::map<std::string, std::string> pendingChallenges; // Challenger -> Target
    std::vector<std::string> matchmakingQueue; // Users waiting for match (Username)
    void processMatchmaking();
    
    // Security
    std::map<std::string, int> failedLoginAttempts; // IP -> Count
    std::map<std::string, std::chrono::steady_clock::time_point> ipLockout; // IP -> UnlockTime
    
    // Helper to find socket by username
    std::shared_ptr<asio::ip::tcp::socket> getSocketByUsername(const std::string& username);
};

}
