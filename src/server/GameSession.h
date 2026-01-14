#define ASIO_STANDALONE
#include <asio.hpp>
#include <string>
#include <vector>
#include <deque>
#include <chrono>
#include <memory>
#include "../common/Protocol.h"

namespace Buckshot {

class GameSession {
public:
    GameSession(const std::string& p1, const std::string& p2, std::shared_ptr<asio::ip::tcp::socket> p1Sock, std::shared_ptr<asio::ip::tcp::socket> p2Sock);
    
    // Core Logic
    void startRound();
    void processMove(const std::string& player, MoveType move, ItemType item = ITEM_NONE);
    void resign(const std::string& player);
    bool checkTimeout(long long timeoutSeconds); // Returns true if timed out
    void setEloChanges(int p1Delta, int p2Delta);
    
    // Getters
    GameStatePacket getState() const;
    bool isGameOver() const;
    std::string getCurrentTurnUser() const;
    std::shared_ptr<asio::ip::tcp::socket> getP1Socket() const { return p1Socket; }
    std::shared_ptr<asio::ip::tcp::socket> getP2Socket() const { return p2Socket; }
    std::string getP1Name() const { return p1Name; }
    std::string getP2Name() const { return p2Name; }
    std::vector<GameStatePacket> getHistory() const { return history; }
    
    // AI
    bool isAiGame() const { return p2Socket == nullptr; }
    bool executeAiTurn();
    
    // Pause
    void togglePause();
    bool isPaused() const { return paused; }

private:
    std::string p1Name, p2Name;
    std::shared_ptr<asio::ip::tcp::socket> p1Socket;
    std::shared_ptr<asio::ip::tcp::socket> p2Socket;
    
    std::vector<GameStatePacket> history; 
    std::chrono::steady_clock::time_point lastActionTime; 
    int itemsUsedThisTurn = 0;
    int eloChangeP1 = 0;
    int eloChangeP2 = 0;
    
    int hp1, hp2;
    std::deque<bool> shells; // true = live, false = blank
    int totalLive;
    int totalBlank;
    std::string currentTurn;
    std::string lastMessage;
    bool gameOver;
    std::string winner;
    
    // Items
    std::vector<ItemType> p1Items;
    std::vector<ItemType> p2Items;
    bool p1Handcuffed;
    bool p2Handcuffed;
    bool knifeActive;
    bool inverterActive; // Flips the next shell logic (virtual flip)
    bool paused = false;
    int32_t pausedTimeRemaining = 0; // Stored time when paused

    void loadShells();
    void distributeItems();
    void useItem(const std::string& player, ItemType item);
    
    // AI Memory
    enum AiShellState { AI_UNKNOWN, AI_KNOWN_LIVE, AI_KNOWN_BLANK };
    AiShellState aiKnownShellState = AI_UNKNOWN;
};

}
