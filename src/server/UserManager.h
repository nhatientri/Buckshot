#pragma once
#include <string>
#include <unordered_map>

namespace Buckshot {

struct User {
    std::string username;
    std::string password;
    int wins = 0;
    int losses = 0;
    int elo = 1000;
};

class UserManager {
public:
    bool registerUser(const std::string& username, const std::string& password);
    bool loginUser(const std::string& username, const std::string& password);
    User* getUser(const std::string& username);
    void loadUsers();
    
    // Post-Game
    void recordMatch(const std::string& winner, const std::string& loser);
    std::string getLeaderboard();

private:
    std::unordered_map<std::string, User> users;
    void saveUsers();
    void logMatch(const std::string& winner, const std::string& loser, int winnerElo, int loserElo);
};

}
