#include "NetworkClient.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <algorithm>

namespace Buckshot {

NetworkClient::NetworkClient() : socketFd(-1), connected(false), loggedIn(false), running(false), loginSuccess(false), loginFailed(false) {}

NetworkClient::~NetworkClient() {
    disconnect();
}

bool NetworkClient::connectToServer(const std::string& ip, int port) {
    if (connected) return true;
    
    socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd < 0) return false;

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) return false;

    if (connect(socketFd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) return false;

    connected = true;
    running = true;
    receiveThread = std::thread(&NetworkClient::receiveLoop, this);
    return true;
}

void NetworkClient::disconnect() {
    running = false;
    if (socketFd >= 0) {
        close(socketFd);
        socketFd = -1;
    }
    if (receiveThread.joinable()) receiveThread.join();
    connected = false;
    loggedIn = false;
}

void NetworkClient::receiveLoop() {
    while (running) {
        if (socketFd < 0) break;
        
        PacketHeader header;
        int valread = read(socketFd, &header, sizeof(header));
        if (valread > 0) {
            std::vector<char> body(header.size);
            if (header.size > 0) {
                // simple blocking read for body
                int totalRead = 0;
                while (totalRead < header.size) {
                     int r = read(socketFd, body.data() + totalRead, header.size - totalRead);
                     if (r <= 0) break;
                     totalRead += r;
                }
            }
            processPacket(header, body);
        } else if (valread <= 0) {
            connected = false;
            running = false;
            break; 
        }
    }
}

void NetworkClient::removeChallenge(size_t index) {
    std::lock_guard<std::mutex> lock(dataMutex);
    if (index < challenges.size()) {
        challenges.erase(challenges.begin() + index);
    }
}


void NetworkClient::processPacket(const PacketHeader& header, const std::vector<char>& body) {
    std::lock_guard<std::mutex> lock(dataMutex);
    std::cout << "[Client] Received Packet Cmd: " << (int)header.command << " Size: " << header.size << std::endl;
    
    if (header.command == CMD_OK || header.command == RES_OK) {
        // Context dependent... assume login/register ok if we were waiting
        // Ideally we'd have request IDs, but for now:
        if (!loggedIn) {
            loginSuccess = true;
            loggedIn = true; // Conservative guess
        }
        lastStatusMessage = "Operation Successful";
    } else if (header.command == CMD_FAIL) {
        if (!loggedIn) loginFailed = true;
        lastStatusMessage = "Operation Failed";
    } else if (header.command == CMD_LIST_USERS_RESP) {
        std::string s(body.begin(), body.end());
        onlineUsers.clear();
        std::stringstream ss(s);
        std::string u;
        while (std::getline(ss, u)) {
            if (!u.empty()) onlineUsers.push_back(u);
        }
    } else if (header.command == CMD_LEADERBOARD_RESP) {
         leaderboardText = std::string(body.begin(), body.end());
    } else if (header.command == CMD_CHALLENGE_REQ) {
        if (body.size() >= sizeof(ChallengePacket)) {
            ChallengePacket* pkt = (ChallengePacket*)body.data();
            std::string challenger = pkt->targetUser;
            if (std::find(challenges.begin(), challenges.end(), challenger) == challenges.end()) {
                challenges.push_back(challenger);
                lastStatusMessage = "New Challenge from " + challenger;
            }
        }
    } else if (header.command == CMD_CHALLENGE_RESP) {
        // Game start logic handles usually by GameState, but we can note it
        lastStatusMessage = "Challenge Accepted!";
    } else if (header.command == CMD_GAME_STATE) {
        if (body.size() >= sizeof(GameStatePacket)) {
            gameState.inGame = true;
            gameState.state = *(GameStatePacket*)body.data();
            lastStateUpdate = std::chrono::steady_clock::now();
            
            if (gameState.state.gameOver) {
                lastStatusMessage = "Game Over. Winner: " + std::string(gameState.state.winner);
                 // Don't set inGame=false immediately, let player see results
            }
        }
    } else if (header.command == CMD_LIST_REPLAYS_RESP) {
        std::string s(body.begin(), body.end());
        replayList.clear();
        std::stringstream ss(s);
        std::string u;
        while (std::getline(ss, u)) {
            if (!u.empty()) replayList.push_back(u);
        }
    } else if (header.command == CMD_REPLAY_DATA) {
        int count = header.size / sizeof(GameStatePacket);
        currentReplay.clear();
        if (count > 0) {
            GameStatePacket* pkts = (GameStatePacket*)body.data();
            for(int i=0; i<count; ++i) currentReplay.push_back(pkts[i]);
        }
        replayReady = true;
        lastStatusMessage = "Replay Downloaded!";
    }
}

// Commands
void NetworkClient::registerUser(const std::string& user, const std::string& pass) {
    std::cout << "[Client] Sending REGISTER for " << user << std::endl;
    myUsername = user;
    LoginRequest req;
    strncpy(req.username, user.c_str(), 32);
    strncpy(req.password, pass.c_str(), 32);
    
    PacketHeader header = {(uint32_t)sizeof(req), CMD_REGISTER};
    send(socketFd, &header, sizeof(header), 0);
    send(socketFd, &req, sizeof(req), 0);
}

void NetworkClient::loginUser(const std::string& user, const std::string& pass) {
    std::cout << "[Client] Sending LOGIN for " << user << std::endl;
    myUsername = user;
    LoginRequest req;
    strncpy(req.username, user.c_str(), 32);
    strncpy(req.password, pass.c_str(), 32);
    
    PacketHeader header = {(uint32_t)sizeof(req), CMD_LOGIN};
    send(socketFd, &header, sizeof(header), 0);
    send(socketFd, &req, sizeof(req), 0);
    
    // Reset flags
    loginSuccess = false;
    loginFailed = false;
}

void NetworkClient::refreshList() {
    PacketHeader header = {0, CMD_LIST_USERS};
    send(socketFd, &header, sizeof(header), 0);
}

void NetworkClient::getLeaderboard() {
    PacketHeader header = {0, CMD_LEADERBOARD};
    send(socketFd, &header, sizeof(header), 0);
}

void NetworkClient::sendChallenge(const std::string& target) {
    ChallengePacket pkt;
    strncpy(pkt.targetUser, target.c_str(), 32);
    PacketHeader header = {(uint32_t)sizeof(pkt), CMD_CHALLENGE_REQ};
    send(socketFd, &header, sizeof(header), 0);
    send(socketFd, &pkt, sizeof(pkt), 0);
}

void NetworkClient::acceptChallenge(const std::string& target) {
    ChallengePacket pkt;
    strncpy(pkt.targetUser, target.c_str(), 32);
    PacketHeader header = {(uint32_t)sizeof(pkt), CMD_CHALLENGE_RESP};
    send(socketFd, &header, sizeof(header), 0);
    send(socketFd, &pkt, sizeof(pkt), 0);
    
    // remove from pending
    std::lock_guard<std::mutex> lock(dataMutex);
    auto it = std::find(challenges.begin(), challenges.end(), target);
    if (it != challenges.end()) challenges.erase(it);
}

void NetworkClient::sendMove(MoveType type, ItemType item) {
    MovePayload p = { (uint8_t)type, (uint8_t)item };
    PacketHeader header = {(uint32_t)sizeof(p), CMD_GAME_MOVE};
    send(socketFd, &header, sizeof(header), 0);
    send(socketFd, &p, sizeof(p), 0);
}

void NetworkClient::sendResign() {
    PacketHeader header = {0, CMD_RESIGN};
    send(socketFd, &header, sizeof(header), 0);
}

// Getters
bool NetworkClient::isConnected() const { return connected; }
bool NetworkClient::isLoggedIn() const { return loggedIn; }

std::string NetworkClient::getLastMessage() {
    std::lock_guard<std::mutex> lock(dataMutex);
    std::string s = lastStatusMessage;
    lastStatusMessage = "";
    return s;
}

std::vector<std::string> NetworkClient::getUserList() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return onlineUsers;
}

std::string NetworkClient::getLeaderboardData() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return leaderboardText;
}

std::vector<std::string> NetworkClient::getPendingChallenges() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return challenges;
}

ClientGameState NetworkClient::getGameState() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return gameState;
}

std::chrono::steady_clock::time_point NetworkClient::getLastStateUpdateTime() const {
    return lastStateUpdate;
}

void NetworkClient::resetGame() {
    std::lock_guard<std::mutex> lock(dataMutex);
    gameState.inGame = false;
    memset(&gameState.state, 0, sizeof(GameStatePacket));
}

void NetworkClient::requestReplayList() {
    PacketHeader header = {0, CMD_LIST_REPLAYS};
    send(socketFd, &header, sizeof(header), 0);
}

std::vector<std::string> NetworkClient::getReplayList() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return replayList;
}

void NetworkClient::requestReplayDownload(const std::string& filename) {
    PacketHeader header = {(uint32_t)filename.size(), CMD_GET_REPLAY};
    send(socketFd, &header, sizeof(header), 0);
    send(socketFd, filename.c_str(), filename.size(), 0);
}

bool NetworkClient::hasReplayData() {
    std::lock_guard<std::mutex> lock(dataMutex);
    bool r = replayReady;
    replayReady = false; // consume check
    return r;
}

std::vector<GameStatePacket> NetworkClient::getReplayData() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return currentReplay;
}

}
