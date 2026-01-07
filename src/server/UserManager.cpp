#include "UserManager.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cmath>

namespace Buckshot {

const std::string USER_DB_FILE = "users.txt";

void UserManager::loadUsers() {
    std::ifstream file(USER_DB_FILE);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        User user;
        ss >> user.username >> user.password >> user.wins >> user.losses >> user.elo;
        users[user.username] = user;
    }
}

void UserManager::saveUsers() {
    std::ofstream file(USER_DB_FILE);
    if (!file.is_open()) return;

    for (const auto& pair : users) {
        const User& u = pair.second;
        file << u.username << " " << u.password << " " << u.wins << " " << u.losses << " " << u.elo << std::endl;
    }
}

bool UserManager::registerUser(const std::string& username, const std::string& password) {
    if (users.find(username) != users.end()) {
        return false; // Already exists
    }
    User newUser;
    newUser.username = username;
    newUser.password = password; // In real app, hash this!
    newUser.wins = 0;
    newUser.losses = 0;
    newUser.elo = 1000;
    
    users[username] = newUser;
    saveUsers();
    return true;
}

bool UserManager::loginUser(const std::string& username, const std::string& password) {
    auto it = users.find(username);
    if (it != users.end()) {
        return it->second.password == password;
    }
    return false;
}

User* UserManager::getUser(const std::string& username) {
    auto it = users.find(username);
    if (it != users.end()) {
        return &(it->second);
    }
    return nullptr;
}

std::pair<int, int> UserManager::recordMatch(const std::string& winnerName, const std::string& loserName) {
    User* winner = getUser(winnerName);
    User* loser = getUser(loserName);
    
    if (!winner || !loser) return {0, 0};
    
    // Elo Calculation (K=32)
    // Ra' = Ra + K * (Sa - Ea)
    // Ea = 1 / (1 + 10 ^ ((Rb - Ra) / 400))
    // Sa = 1 for winner, 0 for loser
    
    double Ra = (double)winner->elo;
    double Rb = (double)loser->elo;
    
    double Ea = 1.0 / (1.0 + pow(10.0, (Rb - Ra) / 400.0));
    double Eb = 1.0 / (1.0 + pow(10.0, (Ra - Rb) / 400.0));
    
    int K = 32;
    int winnerDelta = (int)(K * (1.0 - Ea));
    int loserDelta = (int)(K * (0.0 - Eb)); // usually negative
    
    winner->elo += winnerDelta;
    loser->elo += loserDelta;
    winner->wins++;
    loser->losses++;
    
    saveUsers();
    logMatch(winnerName, loserName, winner->elo, loser->elo);
    
    std::cout << "Match Recorded: " << winnerName << " (+" << winnerDelta << ") vs " << loserName << " (" << loserDelta << ")" << std::endl;
    return {winnerDelta, loserDelta};
}

std::string UserManager::getLeaderboard() {
    std::vector<User> allUsers;
    for (const auto& pair : users) {
        allUsers.push_back(pair.second);
    }
    
    // Sort descending by Elo
    std::sort(allUsers.begin(), allUsers.end(), [](const User& a, const User& b) {
        return a.elo > b.elo;
    });
    
    std::stringstream ss;
    ss << "TOP 10 PLAYERS\n----------------\n";
    int rank = 1;
    for (const auto& u : allUsers) {
        if (rank > 10) break;
        ss << rank << ". " << u.username << " - Elo: " << u.elo << " (W:" << u.wins << " L:" << u.losses << ")\n";
        rank++;
    }
    return ss.str();
}

void UserManager::logMatch(const std::string& winner, const std::string& loser, int winnerElo, int loserElo) {
    std::ofstream file("history.txt", std::ios::app);
    if (!file.is_open()) return;
    file << "WINNER: " << winner << " (" << winnerElo << ") vs LOSER: " << loser << " (" << loserElo << ")" << std::endl;
}

}
