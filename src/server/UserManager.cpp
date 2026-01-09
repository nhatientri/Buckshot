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

// ... (previous code)

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
                      "replay_file TEXT,"
                      "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);"
                      "CREATE TABLE IF NOT EXISTS friends ("
                      "requester TEXT,"
                      "target TEXT,"
                      "status TEXT,"
                      "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
                      "UNIQUE(requester, target));";

    char* errMsg = 0;
    rc = sqlite3_exec(db, sql, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    }
    
    // Auto-migration for replay_file if it doesn't exist
    // Simple way: Try to add it, ignore error
    char* altMsg = 0;
    sqlite3_exec(db, "ALTER TABLE match_history ADD COLUMN replay_file TEXT;", 0, 0, &altMsg);
    if (altMsg) sqlite3_free(altMsg);
    
    // Auto-migration for elo changes
    sqlite3_exec(db, "ALTER TABLE match_history ADD COLUMN winner_elo_change INTEGER DEFAULT 0;", 0, 0, 0);
    sqlite3_exec(db, "ALTER TABLE match_history ADD COLUMN loser_elo_change INTEGER DEFAULT 0;", 0, 0, 0);
}


void UserManager::migrateFromFlatFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string user, pass;
        int w, l, e;
        if (ss >> user >> pass >> w >> l >> e) {
            // Check if exists
            auto existing = getUser(user);
            if (!existing) {
                 registerUser(user, pass);
                 // Update stats
                 std::string sql = "UPDATE users SET wins = ?, losses = ?, elo = ? WHERE username = ?;";
                 sqlite3_stmt* stmt;
                 if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) == SQLITE_OK) {
                     sqlite3_bind_int(stmt, 1, w);
                     sqlite3_bind_int(stmt, 2, l);
                     sqlite3_bind_int(stmt, 3, e);
                     sqlite3_bind_text(stmt, 4, user.c_str(), -1, SQLITE_STATIC);
                     sqlite3_step(stmt);
                     sqlite3_finalize(stmt);
                 }
            }
        }
    }
    file.close();
    // Rename/Delete to prevent re-migration? Or just leave it. getUser check prevents dupes.
}

bool UserManager::registerUser(const std::string& username, const std::string& password) {
    if (getUser(username)) return false; // Already exists

    std::string sql = "INSERT INTO users (username, password, wins, losses, elo) VALUES (?, ?, 0, 0, 1000);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool UserManager::loginUser(const std::string& username, const std::string& password) {
    std::string sql = "SELECT password FROM users WHERE username = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    
    bool valid = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* dbPass = (const char*)sqlite3_column_text(stmt, 0);
        if (dbPass && password == std::string(dbPass)) {
            valid = true;
        }
    }
    sqlite3_finalize(stmt);
    return valid;
}

std::optional<User> UserManager::getUser(const std::string& username) {
    std::string sql = "SELECT username, password, wins, losses, elo FROM users WHERE username = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

    std::optional<User> result = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        User u;
        u.username = (const char*)sqlite3_column_text(stmt, 0);
        u.password = (const char*)sqlite3_column_text(stmt, 1);
        u.wins = sqlite3_column_int(stmt, 2);
        u.losses = sqlite3_column_int(stmt, 3);
        u.elo = sqlite3_column_int(stmt, 4);
        result = u;
    }
    sqlite3_finalize(stmt);
    return result;
}

std::pair<int, int> UserManager::recordMatch(const std::string& winnerName, const std::string& loserName, const std::string& replayFile) {
    auto winnerOpt = getUser(winnerName);
    auto loserOpt = getUser(loserName);

    // Create dummy users if not found (e.g. AI)
    User winner = winnerOpt.value_or(User{winnerName, "", 0, 0, 1000});
    User loser = loserOpt.value_or(User{loserName, "", 0, 0, 1000});

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
    
    // Update Winner if they exist
    if (winnerOpt) {
        sqlite3_stmt* stmtW;
        sqlite3_prepare_v2(db, updateSql.c_str(), -1, &stmtW, 0);
        sqlite3_bind_int(stmtW, 1, winner.wins);
        sqlite3_bind_int(stmtW, 2, winner.losses);
        sqlite3_bind_int(stmtW, 3, winner.elo);
        sqlite3_bind_text(stmtW, 4, winner.username.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmtW);
        sqlite3_finalize(stmtW);
    }

    // Update Loser if they exist
    if (loserOpt) {
        sqlite3_stmt* stmtL;
        sqlite3_prepare_v2(db, updateSql.c_str(), -1, &stmtL, 0);
        sqlite3_bind_int(stmtL, 1, loser.wins);
        sqlite3_bind_int(stmtL, 2, loser.losses);
        sqlite3_bind_int(stmtL, 3, loser.elo);
        sqlite3_bind_text(stmtL, 4, loser.username.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmtL);
        sqlite3_finalize(stmtL);
    }

    sqlite3_exec(db, "COMMIT;", 0, 0, &errMsg);

    logMatch(winnerName, loserName, winnerDelta, loserDelta, replayFile);
    
    std::cout << "Match Recorded (DB): " << winnerName << " (+" << winnerDelta << ") vs " << loserName << " (" << loserDelta << ")" << std::endl;
    
    return {winnerDelta, loserDelta};
}

void UserManager::logMatch(const std::string& winner, const std::string& loser, int winnerDelta, int loserDelta, const std::string& replayFile) {
    std::string sql = "INSERT INTO match_history (winner, loser, winner_elo_change, loser_elo_change, replay_file) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, winner.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, loser.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, winnerDelta);
        sqlite3_bind_int(stmt, 4, loserDelta);
        sqlite3_bind_text(stmt, 5, replayFile.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<HistoryEntry> UserManager::getHistory(const std::string& username) {
    std::vector<HistoryEntry> history;
    // Query where user is winner OR loser
    std::string sql = "SELECT timestamp, winner, loser, winner_elo_change, loser_elo_change, replay_file FROM match_history WHERE winner = ? OR loser = ? ORDER BY id DESC LIMIT 20;";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) return history;
    
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_STATIC);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        HistoryEntry entry;
        const char* ts = (const char*)sqlite3_column_text(stmt, 0);
        const char* w = (const char*)sqlite3_column_text(stmt, 1);
        const char* l = (const char*)sqlite3_column_text(stmt, 2);
        int wDelta = sqlite3_column_int(stmt, 3);
        int lDelta = sqlite3_column_int(stmt, 4);
        const char* rf = (const char*)sqlite3_column_text(stmt, 5);
        
        strncpy(entry.timestamp, ts ? ts : "", 32);
        if (rf) strncpy(entry.replayFile, rf, 64);
        else memset(entry.replayFile, 0, 64);
        
        std::string winner(w);
        std::string loser(l);
        
        if (username == winner) {
            strncpy(entry.opponent, loser.c_str(), 32);
            strncpy(entry.result, "WIN", 8);
            entry.eloChange = wDelta;
        } else {
            strncpy(entry.opponent, winner.c_str(), 32);
            strncpy(entry.result, "LOSS", 8);
            entry.eloChange = lDelta;
        }
        history.push_back(entry);
    }
    sqlite3_finalize(stmt);
    return history;
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

bool UserManager::addFriendRequest(const std::string& user, const std::string& friendName) {
    // 1. Check if user exists
    if (!getUser(friendName)) return false; 
    if (user == friendName) return false;

    // 2. Check overlap
    // If (user, friend) exists -> fail (already requested)
    // If (friend, user) exists AND pending -> Auto Accept? Or fail and say "They already invited you"
    
    // Simplest: Check if ANY relationship exists
    std::string checkSql = "SELECT status FROM friends WHERE (requester=? AND target=?) OR (requester=? AND target=?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, checkSql.c_str(), -1, &stmt, 0) != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, friendName.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, friendName.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, user.c_str(), -1, SQLITE_STATIC);
    
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    
    if (exists) return false; // Already related

    // 3. Insert PENDING
    std::string sql = "INSERT INTO friends (requester, target, status) VALUES (?, ?, 'PENDING');";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, friendName.c_str(), -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool UserManager::acceptFriendRequest(const std::string& user, const std::string& friendName) {
    // User is accepting a request FROM friendName
    std::string sql = "UPDATE friends SET status='ACCEPTED' WHERE requester=? AND target=? AND status='PENDING';";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, friendName.c_str(), -1, SQLITE_STATIC); // Friend is requester
    sqlite3_bind_text(stmt, 2, user.c_str(), -1, SQLITE_STATIC);       // User is target
    
    int rc = sqlite3_step(stmt);
    int changed = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE && changed > 0);
}

bool UserManager::removeFriend(const std::string& user, const std::string& friendName) {
    std::string sql = "DELETE FROM friends WHERE (requester=? AND target=?) OR (requester=? AND target=?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, friendName.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, friendName.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, user.c_str(), -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    int changed = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE && changed > 0);
}

std::string UserManager::getFriendList(const std::string& user) {
    std::string list;
    
    // Find all relationships
    std::string sql = "SELECT requester, target, status FROM friends WHERE requester=? OR target=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) return "";
    
    sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user.c_str(), -1, SQLITE_STATIC);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string r = (const char*)sqlite3_column_text(stmt, 0);
        std::string t = (const char*)sqlite3_column_text(stmt, 1);
        std::string s = (const char*)sqlite3_column_text(stmt, 2);
        
        std::string other = (r == user) ? t : r;
        
        // Format: "FriendName:STATUS"
        // But for UI, we might want to know if WE sent it or THEY sent it (for Pending)
        // If s == ACCEPTED -> "FriendName:ACCEPTED"
        // If s == PENDING:
        //    If r == user (We sent) -> "FriendName:SENT"
        //    If t == user (They sent) -> "FriendName:PENDING"
        
        std::string statusStr = s;
        if (s == "PENDING") {
            if (r == user) statusStr = "SENT";
            else statusStr = "PENDING"; // Incoming
        }
        
        if (!list.empty()) list += ",";
        list += other + ":" + statusStr;
    }
    sqlite3_finalize(stmt);
    return list;
}

}
