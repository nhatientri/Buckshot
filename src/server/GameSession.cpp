#include "GameSession.h"
#include <iostream>
#include <algorithm>
#include <random>
#include <cstring>

namespace Buckshot {

GameSession::GameSession(const std::string& p1, const std::string& p2, int fd1, int fd2)
    : p1Name(p1), p2Name(p2), p1Fd(fd1), p2Fd(fd2), hp1(5), hp2(5), gameOver(false),
      p1Handcuffed(false), p2Handcuffed(false), knifeActive(false), inverterActive(false) {
    
    currentTurn = p1Name; // P1 starts
    lastActionTime = std::chrono::steady_clock::now();
    loadShells();
}

void GameSession::loadShells() {
    shells.clear();
    static std::random_device rd;
    static std::mt19937 gen(rd());
    
    // Random number of shells (2-8)
    std::uniform_int_distribution<> countDist(2, 8);
    int count = countDist(gen);
    
    // At least 1 live and 1 blank if count >= 2
    shells.push_back(true);
    shells.push_back(false);
    
    std::uniform_int_distribution<> typeDist(0, 1);
    for (int i = 2; i < count; ++i) {
        shells.push_back(typeDist(gen) == 1);
    }
    
    std::shuffle(shells.begin(), shells.end(), gen);
    
    // Count totals
    totalLive = 0;
    totalBlank = 0;
    for (bool s : shells) {
        if (s) totalLive++; else totalBlank++;
    }
    
    lastMessage += " Loaded " + std::to_string(count) + " shells (" + std::to_string(totalLive) + " Live, " + std::to_string(totalBlank) + " Blank)";
    
    distributeItems();
}

void GameSession::distributeItems() {
    // Distribute fixed 3 items, max 6 in inventory
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> itemDist(1, 7); // 1-7 (ItemType enum)

    int count = 3;
    for (int i=0; i<count; ++i) {
        if (p1Items.size() < 6) p1Items.push_back((ItemType)itemDist(gen));
        if (p2Items.size() < 6) p2Items.push_back((ItemType)itemDist(gen));
    }
    lastMessage += " Items distributed.";
}

void GameSession::useItem(const std::string& player, ItemType item) {
    std::vector<ItemType>& inventory = (player == p1Name) ? p1Items : p2Items;
    auto it = std::find(inventory.begin(), inventory.end(), item);
    if (it == inventory.end()) {
        lastMessage = player + " tried to use invalid item!";
        return;
    }
    
    inventory.erase(it);
    lastMessage = player + " used ";

    if (item == ITEM_BEER) {
        lastMessage += "BEER. ";
        if (!shells.empty()) {
            bool shell = shells.front();
            shells.pop_front();
            lastMessage += "Ejected a " + std::string(shell ? "LIVE" : "BLANK") + " round.";
        } else {
            lastMessage += "But gun was empty!";
        }
    } else if (item == ITEM_CIGARETTES) {
        lastMessage += "CIGARETTES. ";
        if (player == p1Name) {
            if (hp1 < 5) { hp1++; lastMessage += "+1 HP."; }
            else lastMessage += "HP Full!";
        } else {
            if (hp2 < 5) { hp2++; lastMessage += "+1 HP."; }
            else lastMessage += "HP Full!";
        }
    } else if (item == ITEM_HANDCUFFS) {
        lastMessage += "HANDCUFFS. Opponent skips next turn.";
        if (player == p1Name) p2Handcuffed = true; else p1Handcuffed = true;
    } else if (item == ITEM_MAGNIFYING_GLASS) {
        lastMessage += "MAGNIFYING GLASS. ";
        if (!shells.empty()) {
            bool next = shells.front();
            // Inverter now modifies shells directly, so no need to check flag here
            // Actually Inverter flips it physically when fired? Or flips the nature?
            // "Inverter (Flips the next shell)" -> implying physical change or logical swap?
            // Let's assume logical swap for the next shot. Glass should probably show the *current* state.
            // If inverter already active, show the inverted state? Let's say yes for consistency.
            lastMessage += "Next shell is " + std::string(next ? "LIVE" : "BLANK") + ".";
        }
    } else if (item == ITEM_KNIFE) {
        lastMessage += "KNIFE. Next shot double damage.";
        knifeActive = true;
    } else if (item == ITEM_INVERTER) {
        lastMessage += "INVERTER. Polarity flipped.";
        if (!shells.empty()) {
            shells.front() = !shells.front();
        }
    } else if (item == ITEM_EXPIRED_MEDICINE) {
        lastMessage += "MEDICINE. ";
        static std::random_device rd;
        static std::mt19937 gen(rd());
        if (gen() % 2 == 0) {
            lastMessage += "Healed 2 HP!";
            if (player == p1Name) {
                hp1 += 2; 
                if (hp1 > 5) hp1 = 5;
            } else {
                hp2 += 2;
                if (hp2 > 5) hp2 = 5;
            }
        } else {
            lastMessage += "Lost 1 HP!";
            if (player == p1Name) hp1--; else hp2--;
            // TODO: Check death here? 
             if (hp1 <= 0 || hp2 <= 0) {
                gameOver = true;
                winner = (hp1 <= 0) ? p2Name : p1Name;
            }
        }
    }
}

void GameSession::processMove(const std::string& player, MoveType move, ItemType item) {
    if (gameOver || player != currentTurn) return;
    
    lastActionTime = std::chrono::steady_clock::now();
    
    if (shells.empty()) loadShells(); 

    if (move == USE_ITEM) {
        useItem(player, item);
        return; // Turn does not end on item use
    }

    // Shooting Logic
    bool isLive = shells.front();
    shells.pop_front();
    
    // Inverter logic removed: data is modified directly in useItem
    // if (inverterActive) { ... }
    
    int damage = knifeActive ? 2 : 1;
    knifeActive = false; // Consumed
    
    std::string shellType = isLive ? "LIVE" : "BLANK";
    lastMessage = player + " shot ";
    
    bool switchTurn = true;
    bool handcuffsActive = (player == p1Name) ? p2Handcuffed : p1Handcuffed;

    if (move == SHOOT_SELF) {
        lastMessage += "THEMSELVES. It was " + shellType + ".";
        if (isLive) {
            if (player == p1Name) hp1 -= damage; else hp2 -= damage;
        } else {
            // Shoots self with blank -> Extra turn
            switchTurn = false; 
            lastMessage += " Extra turn!";
        }
    } else {
        std::string opponent = (player == p1Name) ? p2Name : p1Name;
        lastMessage += opponent + ". It was " + shellType + ".";
        if (isLive) {
            if (player == p1Name) hp2 -= damage; else hp1 -= damage;
        }
    }
    
    // Handcuff logic: if turn needs to switch, but opponent is handcuffed, skip them.
    if (switchTurn && handcuffsActive) {
        lastMessage += " Opponent was HANDCUFFED. Turn skipped!";
        // Consumed handcuffs
        if (player == p1Name) p2Handcuffed = false; else p1Handcuffed = false;
        switchTurn = false; // Keep turn
    }

    if (hp1 <= 0) {
        gameOver = true;
        winner = p2Name;
        lastMessage += " " + p1Name + " died!";
    } else if (hp2 <= 0) {
        gameOver = true;
        winner = p1Name;
        lastMessage += " " + p2Name + " died!";
    } else if (switchTurn) {
        currentTurn = (currentTurn == p1Name) ? p2Name : p1Name;
    }
    
    // Reload if empty and game not over
    if (shells.empty() && !gameOver) {
        loadShells();
        lastMessage += " (Reloading...)";
    }
    
    // Record history
    history.push_back(getState());
}

void GameSession::resign(const std::string& player) {
    if (gameOver) return;
    
    gameOver = true;
    if (player == p1Name) {
        winner = p2Name;
        lastMessage = p1Name + " RESIGNED. " + p2Name + " Wins!";
        hp1 = 0; // Force HP to 0 for visual clarity
    } else {
        winner = p1Name;
        lastMessage = p2Name + " RESIGNED. " + p1Name + " Wins!";
        hp2 = 0;
    }
    
    history.push_back(getState());
}

bool GameSession::checkTimeout(long long timeoutSeconds) {
    if (gameOver) return false;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastActionTime).count();
    
    if (elapsed > timeoutSeconds) {
        resign(currentTurn); // Current turn player loses
        lastMessage += " (AFK TIMEOUT)"; 
        return true;
    }
    return false;
}

void GameSession::setEloChanges(int p1Delta, int p2Delta) {
    eloChangeP1 = p1Delta;
    eloChangeP2 = p2Delta;
}

GameStatePacket GameSession::getState() const {
    GameStatePacket pkt;
    pkt.p1Hp = hp1;
    pkt.p2Hp = hp2;
    pkt.shellsRemaining = (int)shells.size();
    
    // Count currently remaining
    int currLive = 0;
    int currBlank = 0;
    for (bool s : shells) {
        if (s) currLive++; else currBlank++;
    }
    pkt.liveCount = currLive;
    pkt.blankCount = currBlank;
    
    pkt.gameOver = gameOver;
    
    // Fill inventory
    std::fill(std::begin(pkt.p1Inventory), std::end(pkt.p1Inventory), ITEM_NONE);
    std::fill(std::begin(pkt.p2Inventory), std::end(pkt.p2Inventory), ITEM_NONE);
    for (size_t i=0; i<p1Items.size() && i<8; ++i) pkt.p1Inventory[i] = (uint8_t)p1Items[i];
    for (size_t i=0; i<p2Items.size() && i<8; ++i) pkt.p2Inventory[i] = (uint8_t)p2Items[i];

    pkt.p1Handcuffed = p1Handcuffed;
    pkt.p2Handcuffed = p2Handcuffed;
    pkt.knifeActive = knifeActive;
    
    strncpy(pkt.currentTurnUser, currentTurn.c_str(), 32);
    strncpy(pkt.p1Name, p1Name.c_str(), 32);
    strncpy(pkt.p2Name, p2Name.c_str(), 32);
    strncpy(pkt.message, lastMessage.c_str(), 64);
    if (gameOver) {
        strncpy(pkt.winner, winner.c_str(), 32);
    } else {
        memset(pkt.winner, 0, 32);
    }
    
    // Time remaining
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastActionTime).count();
    pkt.turnTimeRemaining = (int32_t)(30 - elapsed);
    if (pkt.turnTimeRemaining < 0) pkt.turnTimeRemaining = 0;
    
    pkt.p1EloChange = eloChangeP1;
    pkt.p2EloChange = eloChangeP2;

    return pkt;
}

std::string GameSession::getCurrentTurnUser() const {
    return currentTurn;
}


bool GameSession::isGameOver() const {
    return gameOver;
}

void GameSession::startRound() {
    hp1 = 5;
    hp2 = 5;
    p1Items.clear();
    p2Items.clear();
    lastMessage = "New Round Started!";
    loadShells();
}

}
