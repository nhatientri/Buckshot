#include "ReplayManager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <ctime>
#include <iomanip>

namespace Buckshot {

const std::string REPLAY_DIR = "replays";

std::string ReplayManager::saveReplay(const std::string& p1, const std::string& p2, const std::string& winner, const std::vector<GameStatePacket>& history) {
    if (!std::filesystem::exists(REPLAY_DIR)) {
        std::filesystem::create_directory(REPLAY_DIR);
    }

    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::stringstream ss;
    ss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    std::string timestamp = ss.str();

    // Just return the basename for storage? Or full relative path?
    // Client needs to request it. Let's return just filename.
    std::string filenameBase = timestamp + "_" + winner + "_vs_" + (winner == p1 ? p2 : p1) + ".replay";
    std::string filename = REPLAY_DIR + "/" + filenameBase;

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) return "";

    // Header: P1, P2, Winner, Count
    // Use fixed size for simplicity or length prefixes
    // Let's just write raw data
    size_t count = history.size();
    file.write((char*)&count, sizeof(count));
    file.write((char*)history.data(), count * sizeof(GameStatePacket));
    
    std::cout << "Saved replay to " << filename << std::endl;
    return filenameBase;
}

std::string ReplayManager::getReplayList(const std::string& userFilter) {
    if (!std::filesystem::exists(REPLAY_DIR)) return "";
    
    std::stringstream ss;
    for (const auto& entry : std::filesystem::directory_iterator(REPLAY_DIR)) {
        if (entry.path().extension() == ".replay") {
            std::string filename = entry.path().filename().string();
            if (userFilter.empty()) {
                ss << filename << "\n";
            } else {
                // Check if user is in the filename (simple check)
                // Filename format: YYYYMMDD_HHMMSS_Winner_vs_Loser.replay
                // We just check if username exists in the string (surrounded by _, or at end)
                // Actually simple substring check might be enough if usernames don't overlap much.
                // But to be safer, we can parse or check specific patterns.
                // Simpler: Check if `_user_` or `_user.replay` or `_user_vs` exists?
                // The format is unpredictable with usernames.
                // However, `Winner_vs_Loser`.
                // If I search for `userFilter`, and it is found...
                if (filename.find(userFilter) != std::string::npos) {
                     ss << filename << "\n";
                }
            }
        }
    }
    return ss.str();
}

std::vector<GameStatePacket> ReplayManager::loadReplay(const std::string& filename) {
    std::vector<GameStatePacket> history;
    std::string path = REPLAY_DIR + "/" + filename;
    
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return history;
    
    size_t count = 0;
    file.read((char*)&count, sizeof(count));
    
    if (count > 0) {
        history.resize(count);
        file.read((char*)history.data(), count * sizeof(GameStatePacket));
    }
    
    return history;
}

}
