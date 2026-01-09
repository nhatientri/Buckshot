#pragma once
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include "../common/Protocol.h"

namespace Buckshot {

struct ClientGameState {
    bool inGame = false;
    GameStatePacket state;
};

class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();

    bool connectToServer(const std::string& ip, int port);
    void disconnect();
    
    // Actions
    void registerUser(const std::string& user, const std::string& pass);
    void loginUser(const std::string& user, const std::string& pass);
    void refreshList();
    void getLeaderboard();
    void sendChallenge(const std::string& target);
    void acceptChallenge(const std::string& target);
    void sendMove(MoveType type, ItemType item = ITEM_NONE);
    void sendResign();
    
    // State Access (Thread Safe)
    bool isConnected() const;
    bool isLoggedIn() const;
    std::string getUsername() const { return myUsername; }
    
    std::string getLastMessage(); // Consumes message
    std::vector<std::string> getUserList();
    std::string getLeaderboardData();
    std::vector<std::string> getPendingChallenges(); // "Incoming challenge from X" events
    void removeChallenge(size_t index); // Remove locally
    std::string getRematchTarget();
    
    ClientGameState getGameState();
    std::chrono::steady_clock::time_point getLastStateUpdateTime() const;
    void resetGame();
    
    // Replay
    void requestReplayList();
    std::vector<std::string> getReplayList(); // Returns cached list
    void requestReplayDownload(const std::string& filename);
    bool hasReplayData();
    std::vector<GameStatePacket> getReplayData(); // Consume replay data
    
    void sendPlayAiRequest();
    
    // Matchmaking
    void sendJoinQueue();
    void sendLeaveQueue();
    
    // Pause
    // Pause
    void sendTogglePause();
    
    // History
    void requestHistory();
    std::vector<HistoryEntry> getHistory();

    // Friends
    void requestFriendList();
    void sendAddFriend(const std::string& friendName);
    void sendAcceptFriend(const std::string& friendName);
    void sendRemoveFriend(const std::string& friendName);
    
    // Returns parsed "Name:Status" strings
    std::vector<std::string> getFriendList();
    // Returns pending friend request names (incoming)
    std::vector<std::string> getIncomingFriendRequests();
    void clearIncomingFriendRequests();
    
    // Status flags used by UI to show popups/errors
    std::atomic<bool> loginSuccess;
    std::atomic<bool> loginFailed;
    
private:
    int socketFd;
    std::atomic<bool> connected;
    std::atomic<bool> loggedIn;
    std::string myUsername;
    
    std::thread receiveThread;
    std::atomic<bool> running;
    
    void receiveLoop();
    void processPacket(const PacketHeader& header, const std::vector<char>& body);
    
    // Data Guards
    std::mutex dataMutex;
    std::string lastStatusMessage; // For bottom bar or errors
    std::vector<std::string> onlineUsers;
    std::string leaderboardText;
    
    std::vector<std::string> replayList;
    std::vector<GameStatePacket> currentReplay;
    std::vector<HistoryEntry> history;
    std::vector<std::string> friendList; 
    std::vector<std::string> incomingFriendRequests; // Just names
    bool replayReady;
    
    std::string lastOpponent;
    
    std::vector<std::string> challenges;
    ClientGameState gameState;
    std::chrono::steady_clock::time_point lastStateUpdate;
};

}
