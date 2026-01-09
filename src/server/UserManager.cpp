#include "UserManager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace Buckshot {

UserManager::UserManager() {
    initDatabase();
    // Auto-migrate if users.txt exists
    migrateFromFlatFile("users.txt");
}

UserManager::~UserManager() {
    if (db) {
        sqlite3_close(db);
    }
}

void UserManager::initDatabase() {
    int rc = sqlite3_open("buckshot.db", &db);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

    const char* sql = "CREATE TABLE IF NOT EXISTS users ("
                      "username TEXT PRIMARY KEY,"
                      "password TEXT NOT NULL,"
                      "wins INTEGER DEFAULT 0,"
                      "losses INTEGER DEFAULT 0,"
                      "elo INTEGER DEFAULT 1000);"
                      "CREATE TABLE IF NOT EXISTS match_history ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "winner TEXT,"
                      "loser TEXT,"
                      "winner_elo INTEGER,"
                      "loser_elo INTEGER,"
                      "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";

    char* errMsg = 0;
    rc = sqlite3_exec(db, sql, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    }
}

void UserManager::migrateFromFlatFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return;

    std::cout << "Migrating " << filepath << " to SQLite..." << std::endl;
    
    // Wrap in transaction for speed
    char* errMsg = 0;
    sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, &errMsg);

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string u, p;
        int w, l, e;
        if (ss >> u >> p >> w >> l >> e) {
            std::string sql = "INSERT OR IGNORE INTO users (username, password, wins, losses, elo) VALUES (?, ?, ?, ?, ?);";
            sqlite3_stmt* stmt;
            sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0);
            sqlite3_bind_text(stmt, 1, u.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, p.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 3, w);
            sqlite3_bind_int(stmt, 4, l);
            sqlite3_bind_int(stmt, 5, e);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    sqlite3_exec(db, "COMMIT;", 0, 0, &errMsg);
    file.close();

    // Rename file so we don't migrate again
    std::string newName = filepath + ".bak";
    rename(filepath.c_str(), newName.c_str());
    std::cout << "Migration complete. Renamed to " << newName << std::endl;
}

bool UserManager::registerUser(const std::string& username, const std::string& password) {
    std::string sql = "INSERT INTO users (username, password) VALUES (?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool UserManager::loginUser(const std::string& username, const std::string& password) {
    std::string sql = "SELECT password FROM users WHERE username = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

    bool result = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* dbPass = (const char*)sqlite3_column_text(stmt, 0);
        if (dbPass && std::string(dbPass) == password) {
            result = true;
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<User> UserManager::getUser(const std::string& username) {
    std::string sql = "SELECT wins, losses, elo, password FROM users WHERE username = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

    std::optional<User> user = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        User u;
        u.username = username;
        u.wins = sqlite3_column_int(stmt, 0);
        u.losses = sqlite3_column_int(stmt, 1);
        u.elo = sqlite3_column_int(stmt, 2);
        const char* p = (const char*)sqlite3_column_text(stmt, 3);
        u.password = p ? p : "";
        user = u;
    }
    sqlite3_finalize(stmt);
    return user;
}

std::pair<int, int> UserManager::recordMatch(const std::string& winnerName, const std::string& loserName) {
    auto winnerOpt = getUser(winnerName);
    auto loserOpt = getUser(loserName);

    if (!winnerOpt || !loserOpt) return {0, 0};

    User winner = *winnerOpt;
    User loser = *loserOpt;

    // Elo Config
    double Ra = (double)winner.elo;
    double Rb = (double)loser.elo;
    double Ea = 1.0 / (1.0 + pow(10.0, (Rb - Ra) / 400.0));
    double Eb = 1.0 / (1.0 + pow(10.0, (Ra - Rb) / 400.0));
    int K = 32;
    int winnerDelta = (int)(K * (1.0 - Ea));
    int loserDelta = (int)(K * (0.0 - Eb));

    winner.elo += winnerDelta;
    loser.elo += loserDelta;
    winner.wins++;
    loser.losses++;

    // Transactional Update
    char* errMsg = 0;
    sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, &errMsg);

    std::string updateSql = "UPDATE users SET wins = ?, losses = ?, elo = ? WHERE username = ?;";
    
    // Update Winner
    sqlite3_stmt* stmtW;
    sqlite3_prepare_v2(db, updateSql.c_str(), -1, &stmtW, 0);
    sqlite3_bind_int(stmtW, 1, winner.wins);
    sqlite3_bind_int(stmtW, 2, winner.losses);
    sqlite3_bind_int(stmtW, 3, winner.elo);
    sqlite3_bind_text(stmtW, 4, winner.username.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmtW);
    sqlite3_finalize(stmtW);

    // Update Loser
    sqlite3_stmt* stmtL;
    sqlite3_prepare_v2(db, updateSql.c_str(), -1, &stmtL, 0);
    sqlite3_bind_int(stmtL, 1, loser.wins);
    sqlite3_bind_int(stmtL, 2, loser.losses);
    sqlite3_bind_int(stmtL, 3, loser.elo);
    sqlite3_bind_text(stmtL, 4, loser.username.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmtL);
    sqlite3_finalize(stmtL);

    sqlite3_exec(db, "COMMIT;", 0, 0, &errMsg);

    logMatch(winnerName, loserName, winner.elo, loser.elo);
    std::cout << "Match Recorded (DB): " << winnerName << " (+" << winnerDelta << ") vs " << loserName << " (" << loserDelta << ")" << std::endl;
    
    return {winnerDelta, loserDelta};
}

void UserManager::logMatch(const std::string& winner, const std::string& loser, int winnerElo, int loserElo) {
    std::string sql = "INSERT INTO match_history (winner, loser, winner_elo, loser_elo) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, winner.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, loser.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, winnerElo);
        sqlite3_bind_int(stmt, 4, loserElo);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::string UserManager::getLeaderboard() {
    std::string sql = "SELECT username, elo, wins, losses FROM users ORDER BY elo DESC LIMIT 10;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) return "Error getting leaderboard";

    std::stringstream ss;
    ss << "TOP 10 PLAYERS\n----------------\n";
    int rank = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* u = (const char*)sqlite3_column_text(stmt, 0);
        int elo = sqlite3_column_int(stmt, 1);
        int wins = sqlite3_column_int(stmt, 2);
        int losses = sqlite3_column_int(stmt, 3);
        
        ss << rank << ". " << (u ? u : "Unknown") << " - Elo: " << elo << " (W:" << wins << " L:" << losses << ")\n";
        rank++;
    }
    sqlite3_finalize(stmt);
    return ss.str();
}

}
