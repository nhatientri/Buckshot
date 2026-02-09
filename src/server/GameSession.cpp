#include "GameSession.h"
#include <iostream>
#include <algorithm>
#include <random>
#include <cstring>

namespace Buckshot {

GameSession::GameSession(const std::string& p1, const std::string& p2, int p1Sock, int p2Sock, int p1EloVal, int p2EloVal)
    : p1Name(p1), p2Name(p2), p1Socket(p1Sock), p2Socket(p2Sock), p1Elo(p1EloVal), p2Elo(p2EloVal), hp1(5), hp2(5), gameOver(false),
      p1Handcuffed(false), p2Handcuffed(false), knifeActive(false), inverterActive(false), itemsUsedThisTurn(0) {
    
    currentTurn = p1Name; // P1 starts
    lastActionTime = std::chrono::steady_clock::now();
    loadShells();
}

/* [ASIO REFERENCE]
GameSession::GameSession(const std::string& p1, const std::string& p2, std::shared_ptr<asio::ip::tcp::socket> p1Sock, std::shared_ptr<asio::ip::tcp::socket> p2Sock)
    : p1Name(p1), p2Name(p2), p1Socket(p1Sock), p2Socket(p2Sock), ... {
      // ...
    }
*/

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
    aiKnownShellState = AI_UNKNOWN; // Reset memory on reload
    
    // Record state immediately so Replay sees the new items/shells BEFORE any move consumes them
    history.push_back(getState());
}

void GameSession::distributeItems() {
    // Distribute fixed 3 items, max 8 in inventory (matches UI)
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> itemDist(1, 7); // 1-7 (ItemType enum)

    // Ensure vectors are sized to 6
    if (p1Items.size() < 6) p1Items.resize(6, ITEM_NONE);
    if (p2Items.size() < 6) p2Items.resize(6, ITEM_NONE);

    // Helper to add items
    auto addItems = [&](std::vector<ItemType>& inv) {
        int currentCount = 0;
        for (auto t : inv) if (t != ITEM_NONE) currentCount++;

        for (int i = 0; i < 3; ++i) { // Add up to 3 items
            if (currentCount >= 6) break;
            
            // Find empty slot
            for (auto& slot : inv) {
                if (slot == ITEM_NONE) {
                    slot = (ItemType)itemDist(gen);
                    currentCount++;
                    break;
                }
            }
        }
    };

    addItems(p1Items);
    addItems(p2Items);

    lastMessage += " Items distributed.";
}

void GameSession::useItem(const std::string& player, ItemType item) {
    std::vector<ItemType>& inventory = (player == p1Name) ? p1Items : p2Items;
    
    // Find the item
    auto it = std::find(inventory.begin(), inventory.end(), item);
    if (it == inventory.end()) {
        lastMessage = player + " tried to use invalid item!";
        return;
    }
    
    // Mark as consumed (ITEM_NONE) instead of erasing to preserve order
    *it = ITEM_NONE; 
    
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
            lastMessage += "Next shell is " + std::string(next ? "LIVE" : "BLANK") + ".";
            
            // AI MEMORY UPDATE
            if (player == p2Name) {
                aiKnownShellState = next ? AI_KNOWN_LIVE : AI_KNOWN_BLANK;
            }
        }
    } else if (item == ITEM_KNIFE) {
        lastMessage += "KNIFE. Next shot double damage.";
        knifeActive = true;
    } else if (item == ITEM_INVERTER) {
        lastMessage += "INVERTER. Polarity flipped.";
        if (!shells.empty()) {
            shells.front() = !shells.front();
            // If AI knew the state, flip memory too
            if (aiKnownShellState != AI_UNKNOWN) {
                aiKnownShellState = (aiKnownShellState == AI_KNOWN_LIVE) ? AI_KNOWN_BLANK : AI_KNOWN_LIVE;
            }
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
    if (gameOver || player != currentTurn || paused) return;
    
    lastActionTime = std::chrono::steady_clock::now();
    
    if (shells.empty()) loadShells(); 

    if (move == USE_ITEM) {
        if (itemsUsedThisTurn >= 2) {
            lastMessage += " Max 2 items per turn!";
            return;
        }
        useItem(player, item);
        itemsUsedThisTurn++;
        return; // Turn does not end on item use
    }

    // Shooting Logic
    itemsUsedThisTurn = 0; // Reset for next turn (whoever it is)

    bool isLive = shells.front();
    shells.pop_front();
    
    // Reset AI Memory since shell is gone
    aiKnownShellState = AI_UNKNOWN;
    
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
    if (paused) return false;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastActionTime).count();
    
    // Bump timeout to 60s minimum effectively if the passed value is small?
    // User complaint "randomly lose" might mean the server loop calls this with a short timer.
    
    if (elapsed > timeoutSeconds) {
        // Double check: if it's the very first turn, give more time?
        resign(currentTurn); // Current turn player loses
        lastMessage += " (AFK TIMEOUT)"; 
        std::cout << "Session timeout: " << currentTurn << " AFK for " << elapsed << "s" << std::endl;
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
    pkt.p1Elo = p1Elo;
    pkt.p2Elo = p2Elo;
    strncpy(pkt.message, lastMessage.c_str(), 128);
    if (gameOver) {
        strncpy(pkt.winner, winner.c_str(), 32);
    } else {
        memset(pkt.winner, 0, 32);
    }
    
    // Time remaining
    if (paused) {
        pkt.turnTimeRemaining = pausedTimeRemaining;
    } else {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastActionTime).count();
        pkt.turnTimeRemaining = (int32_t)(30 - elapsed);
        if (pkt.turnTimeRemaining < 0) pkt.turnTimeRemaining = 0;
    }
    
    pkt.p1EloChange = eloChangeP1;
    pkt.p2EloChange = eloChangeP2;
    pkt.isPaused = paused;

    return pkt;
}

bool GameSession::executeAiTurn() {
    if (gameOver) return false;
    if (paused) return false;
    if (currentTurn != p2Name) return false;
    
    // Simple delay (assuming this is called ~60 times a sec? No, server loop is tight)
    // Actually server loop delay is small.
    // Let's use std::chrono check to allow 2 seconds delay.
    // But `executeAiTurn` is called periodically.
    
    // We need to store "Last AI Action Time".
    // Re-use `lastActionTime` but that tracks turn timeout. 
    // It's fine. 
    
    auto now = std::chrono::steady_clock::now();
    long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastActionTime).count();
    
    if (elapsed < 2000) return false; // Wait 2 seconds before acting
    
    // DECISION LOGIC
    
    // 0. Update Probabilities
    int liveCount = 0;
    int blankCount = 0;
    for (bool s : shells) {
        if (s) liveCount++; else blankCount++;
    }
    int total = liveCount + blankCount;
    if (total == 0) return false; // Should not happen due to reload logic
    
    double pLive = (double)liveCount / total;
    
    // MEMORY OVERRIDE
    if (aiKnownShellState == AI_KNOWN_LIVE) {
        pLive = 1.0;
        liveCount = 1; blankCount = 0; // Virtual certainty
    } else if (aiKnownShellState == AI_KNOWN_BLANK) {
        pLive = 0.0;
        liveCount = 0; blankCount = 1; // Virtual certainty
    }

    // 1. Heal (High Priority if low HP)
    if (hp2 <= 2 && std::count(p2Items.begin(), p2Items.end(), ITEM_CIGARETTES) > 0 && itemsUsedThisTurn < 2) {
        processMove(p2Name, USE_ITEM, ITEM_CIGARETTES);
        return true;
    }
    
    // 2. Scan (If unknown)
    if (aiKnownShellState == AI_UNKNOWN && std::count(p2Items.begin(), p2Items.end(), ITEM_MAGNIFYING_GLASS) > 0 && itemsUsedThisTurn < 2) {
        // Only scan if uncertainty exists (both > 0)
        if (liveCount > 0 && blankCount > 0) {
            processMove(p2Name, USE_ITEM, ITEM_MAGNIFYING_GLASS);
            return true;
        }
    }
    
    // 3. Handcuff (If we plan to shoot opponent and they aren't cuffed)
    // Use if we are confident it's live (so we get another turn effectively? No, handcuffs skip their turn)
    // Use it to prevent them from retaliating if we miss or if we hit.
    if (!p1Handcuffed && std::count(p2Items.begin(), p2Items.end(), ITEM_HANDCUFFS) > 0 && itemsUsedThisTurn < 2) {
         processMove(p2Name, USE_ITEM, ITEM_HANDCUFFS);
         return true;
    }
    
    // 4. Inverter (If we know it's blank, or prob(Blank) is high)
    // If we use inverter, Blank -> Live.
    if (std::count(p2Items.begin(), p2Items.end(), ITEM_INVERTER) > 0 && itemsUsedThisTurn < 2) {
        if (aiKnownShellState == AI_KNOWN_BLANK || (aiKnownShellState == AI_UNKNOWN && pLive < 0.4)) {
            processMove(p2Name, USE_ITEM, ITEM_INVERTER);
            return true;
        }
    }
    
    // 5. Beer (If we want to skip a shell)
    // Skip if we know it's blank and we want to shoot, OR if we want to drain shells?
    // Let's say: use beer if we think it's blank, to dig for a live one to shoot opponent.
    if (std::count(p2Items.begin(), p2Items.end(), ITEM_BEER) > 0 && itemsUsedThisTurn < 2) {
        if (aiKnownShellState == AI_KNOWN_BLANK || (aiKnownShellState == AI_UNKNOWN && pLive < 0.5)) {
             processMove(p2Name, USE_ITEM, ITEM_BEER);
             return true;
        }
    }

    // 6. Knife (Only use if we are VERY sure it is Live)
    if (!knifeActive && std::count(p2Items.begin(), p2Items.end(), ITEM_KNIFE) > 0 && itemsUsedThisTurn < 2) {
        if (aiKnownShellState == AI_KNOWN_LIVE || (aiKnownShellState == AI_UNKNOWN && pLive > 0.60)) {
            processMove(p2Name, USE_ITEM, ITEM_KNIFE);
            return true;
        }
    }
    
    // 7. SHOOTING LOGIC
    // If Live is more likely -> Shoot Opponent
    // If Blank is more likely -> Shoot Self (to get extra turn)
    
    if (pLive >= 0.5) {
        processMove(p2Name, SHOOT_OPPONENT);
    } else {
        processMove(p2Name, SHOOT_SELF);
    }
    
    return true;
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
    itemsUsedThisTurn = 0;
    lastMessage = "New Round Started!";
    lastActionTime = std::chrono::steady_clock::now(); // Reset timer
    loadShells();
}

void GameSession::togglePause() {
    paused = !paused;
    auto now = std::chrono::steady_clock::now();
    
    if (paused) {
        // Calculate remaining time and freeze it
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastActionTime).count();
        pausedTimeRemaining = (int32_t)(30 - elapsed);
        if (pausedTimeRemaining < 0) pausedTimeRemaining = 0;
        
        lastMessage += " (PAUSED)";
    } else {
        // Resume: Shift lastActionTime so that (now - lastActionTime) equals the resumed duration
        // We want: 30 - (now - new_lastActionTime) = pausedTimeRemaining
        // => now - new_lastActionTime = 30 - pausedTimeRemaining
        // => new_lastActionTime = now - (30 - pausedTimeRemaining)
        
        lastActionTime = now - std::chrono::seconds(30 - pausedTimeRemaining);

        lastMessage += " (RESUMED)";
    }
}

}
