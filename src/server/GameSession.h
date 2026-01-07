#pragma once
#include <string>
#include <vector>
#include <deque>
#include "../common/Protocol.h"

namespace Buckshot {

class GameSession {
public:
    GameSession(const std::string& p1, const std::string& p2, int p1Fd, int p2Fd);
    
    // Core Logic
    void startRound();
    void processMove(const std::string& player, MoveType move, ItemType item = ITEM_NONE);
    
    // Getters
    GameStatePacket getState() const;
    bool isGameOver() const;
    std::string getCurrentTurnUser() const;
    int getP1Fd() const { return p1Fd; }
    int getP2Fd() const { return p2Fd; }
    std::string getP1Name() const { return p1Name; }
    std::string getP2Name() const { return p2Name; }

private:
    std::string p1Name, p2Name;
    int p1Fd, p2Fd;
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

    void loadShells();
    void distributeItems();
    void useItem(const std::string& player, ItemType item);
};

}
