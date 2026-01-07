#ifndef REPLAY_MANAGER_H
#define REPLAY_MANAGER_H

#include <string>
#include <vector>
#include "../common/Protocol.h"

namespace Buckshot {

class ReplayManager {
public:
    static void saveReplay(const std::string& p1, const std::string& p2, const std::string& winner, const std::vector<GameStatePacket>& history);
    static std::string getReplayList(const std::string& userFilter = "");
    static std::vector<GameStatePacket> loadReplay(const std::string& filename);
};

}

#endif
