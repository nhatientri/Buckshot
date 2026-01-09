#pragma once
#include <string>
#include <sqlite3.h>
#include <optional>
#include <vector>

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
    UserManager();
    ~UserManager();

    bool registerUser(const std::string& username, const std::string& password);
    bool loginUser(const std::string& username, const std::string& password);
    std::optional<User> getUser(const std::string& username);
    
    // Returns pair<int, int> -> (winnerDelta, loserDelta)
    std::pair<int, int> recordMatch(const std::string& winner, const std::string& loser);
    std::string getLeaderboard();

    // Migration
    void migrateFromFlatFile(const std::string& filepath);

private:
    sqlite3* db = nullptr;
    
    void initDatabase();
    void logMatch(const std::string& winner, const std::string& loser, int winnerElo, int loserElo);
};

}
