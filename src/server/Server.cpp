#include "Server.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <cstring>
#include "../common/Protocol.h"
#include "ReplayManager.h"

namespace Buckshot {

// Helper: Synchronous write for simplicity in migration
void sendPacket(std::shared_ptr<asio::ip::tcp::socket> socket, const void* data, size_t size) {
    if (!socket) return;
    try {
        asio::write(*socket, asio::buffer(data, size));
    } catch (std::exception& e) {
        // Drop packet if send fails
    }
}

Server::Server(int port) 
    : port(port), running(false), 
      io_context(), acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
      gameloop_timer(io_context) 
{
    // User migration happens during construction if needed, or controlled by UserManager
    lastTimeoutCheck = std::chrono::steady_clock::now();
    lastMatchmakingBatch = std::chrono::steady_clock::now();
    lastStateBroadcast = std::chrono::steady_clock::now();
}

void Server::run() {
    running = true;
    std::cout << "Server listening on port " << port << " (Asio)..." << std::endl;
    
    doAccept();
    startGameloop();
    
    io_context.run(); 
}

void Server::doAccept() {
    auto socket = std::make_shared<asio::ip::tcp::socket>(io_context);
    acceptor.async_accept(*socket, [this, socket](std::error_code ec) {
        if (!ec) {
            std::cout << "New connection" << std::endl;
            clients.push_back(socket);
            doRead(socket);
        }
        doAccept();
    });
}

void Server::doRead(std::shared_ptr<asio::ip::tcp::socket> socket) {
    auto header = std::make_shared<PacketHeader>();
    asio::async_read(*socket, asio::buffer(header.get(), sizeof(PacketHeader)),
        [this, socket, header](std::error_code ec, std::size_t) {
            if (!ec) {
                if (header->size > 0 && header->size < 100000) { // Basic sanity check
                    auto body = std::make_shared<std::vector<char>>(header->size);
                    asio::async_read(*socket, asio::buffer(*body),
                        [this, socket, header, body](std::error_code ec2, std::size_t) {
                            if (!ec2) {
                                processPacket(socket, *header, *body);
                                doRead(socket);
                            } else {
                                // Error reading body
                                // Handle disconnect
                                auto game = getGameSession(socket);
                                if (game) {
                                    std::string user = authenticatedUsers[socket];
                                    std::cout << "Player " << user << " disconnected." << std::endl;
                                    game->resign(user);
                                }
                                authenticatedUsers.erase(socket);
                                auto it = std::find(clients.begin(), clients.end(), socket);
                                if (it != clients.end()) clients.erase(it);
                            }
                        });
                } else if (header->size == 0) {
                    std::vector<char> empty;
                    processPacket(socket, *header, empty);
                    doRead(socket);
                } else {
                     // Invalid size -> Disconnect
                     authenticatedUsers.erase(socket);
                     auto it = std::find(clients.begin(), clients.end(), socket);
                     if (it != clients.end()) clients.erase(it);
                }
            } else {
                // Disconnect
                // Shared logic for disconnect
                 auto game = getGameSession(socket);
                 if (game) {
                     std::string user = authenticatedUsers[socket];
                     if (!user.empty()) {
                        std::cout << "Player " << user << " disconnected." << std::endl;
                        game->resign(user);
                     }
                 }
                 if (authenticatedUsers.count(socket)) {
                     std::string u = authenticatedUsers[socket];
                     pendingChallenges.erase(u);
                     for (auto it = pendingChallenges.begin(); it != pendingChallenges.end(); ) {
                        if (it->second == u) it = pendingChallenges.erase(it);
                        else ++it;
                     }
                     auto qIt = std::find(matchmakingQueue.begin(), matchmakingQueue.end(), u);
                     if (qIt != matchmakingQueue.end()) matchmakingQueue.erase(qIt);
                 }
                 authenticatedUsers.erase(socket);
                 auto it = std::find(clients.begin(), clients.end(), socket);
                 if (it != clients.end()) clients.erase(it);
                 
                 broadcastUserList();
            }
        });
}

void Server::startGameloop() {
    gameloop_timer.expires_after(std::chrono::milliseconds(100)); // 10Hz tick
    gameloop_timer.async_wait([this](std::error_code ec) {
        if (!ec) {
            // 1. Timeout Checks
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - lastTimeoutCheck).count() >= 1) {
                lastTimeoutCheck = now;
                for (auto it = activeGames.begin(); it != activeGames.end();) {
                    auto& game = *it;
                    if (game->checkTimeout(30)) {
                         std::cout << "Game timed out!" << std::endl;
                         // Broadcast (handled inside timeout block logic below...)
                         GameStatePacket state = game->getState();
                         PacketHeader h = {(uint32_t)sizeof(state), CMD_GAME_STATE};
                         
                         sendPacket(game->getP1Socket(), &h, sizeof(h));
                         sendPacket(game->getP1Socket(), &state, sizeof(state));
                         sendPacket(game->getP2Socket(), &h, sizeof(h));
                         sendPacket(game->getP2Socket(), &state, sizeof(state));
                         
                         // Record
                         std::string winner = game->getState().winner;
                         std::string lose = (winner == game->getP1Name()) ? game->getP2Name() : game->getP1Name();
                         std::string replay = ReplayManager::saveReplay(game->getP1Name(), game->getP2Name(), winner, game->getHistory());
                         auto deltas = userManager.recordMatch(winner, lose, replay);
                         game->setEloChanges((winner==game->getP1Name())?deltas.first : deltas.second, (winner==game->getP2Name())?deltas.first : deltas.second);

                         // Re-broadcast
                         state = game->getState();
                         sendPacket(game->getP1Socket(), &h, sizeof(h));
                         sendPacket(game->getP1Socket(), &state, sizeof(state));
                         sendPacket(game->getP2Socket(), &h, sizeof(h));
                         sendPacket(game->getP2Socket(), &state, sizeof(state));
                         
                         it = activeGames.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            // 1.5 PERIODIC BROADCAST (Sync Timers)
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastStateBroadcast).count() >= 1000) {
                lastStateBroadcast = now;
                for (auto& game : activeGames) {
                     if (!game->isGameOver()) {
                         GameStatePacket state = game->getState();
                         PacketHeader h = {(uint32_t)sizeof(state), CMD_GAME_STATE};
                         sendPacket(game->getP1Socket(), &h, sizeof(h));
                         sendPacket(game->getP1Socket(), &state, sizeof(state));
                         sendPacket(game->getP2Socket(), &h, sizeof(h));
                         sendPacket(game->getP2Socket(), &state, sizeof(state));
                     }
                }
            }
            
            // 2. AI Logic
            for (auto it = activeGames.begin(); it != activeGames.end();) {
                auto& game = *it;
                if (game->isAiGame() && game->executeAiTurn()) {
                     GameStatePacket state = game->getState();
                     PacketHeader h = {(uint32_t)sizeof(state), CMD_GAME_STATE};
                     sendPacket(game->getP1Socket(), &h, sizeof(h));
                     sendPacket(game->getP1Socket(), &state, sizeof(state));
                     
                     if (game->isGameOver()) {
                         std::string winner = game->getState().winner;
                         std::string replay = ReplayManager::saveReplay(game->getP1Name(), game->getP2Name(), winner, game->getHistory());
                         userManager.recordMatch(winner, (winner==game->getP1Name())?game->getP2Name():game->getP1Name(), replay);
                         it = activeGames.erase(it);
                         continue;
                     }
                }
                ++it;
            }
            
            // 3. Matchmaking
            processMatchmaking();
            
            startGameloop();
        }
    });
}

void Server::processMatchmaking() {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastMatchmakingBatch).count() < 5) {
        return; // Batch every 5 seconds
    }
    
    if (matchmakingQueue.size() < 2) return;
    
    lastMatchmakingBatch = now;
    
    // 1. Collect Valid Users
    struct QueueEntry {
        std::string username;
        int elo;
    };
    std::vector<QueueEntry> pool;
    
    for (const auto& uname : matchmakingQueue) {
        auto sock = getSocketByUsername(uname);
        if (sock) {
            auto u = userManager.getUser(uname);
            if (u) {
                pool.push_back({uname, u->elo});
            }
        }
    }
    
    matchmakingQueue.clear(); // Will rebuild
    
    // 2. Sort by ELO
    std::sort(pool.begin(), pool.end(), [](const QueueEntry& a, const QueueEntry& b) {
        return a.elo < b.elo;
    });
    
    // 3. Match Pairs
    while (pool.size() >= 2) {
        QueueEntry p1 = pool.back(); pool.pop_back();
        QueueEntry p2 = pool.back(); pool.pop_back();
        
        auto s1 = getSocketByUsername(p1.username);
        auto s2 = getSocketByUsername(p2.username);

        // Double check functionality (user could disconnect mid-loop)
        if (s1 && s2) {
            std::cout << "Matchmaking (Batch): " << p1.username << " (" << p1.elo << ") vs " << p2.username << " (" << p2.elo << ")" << std::endl;
            auto game = std::make_shared<GameSession>(p1.username, p2.username, s1, s2);
            activeGames.push_back(game);
            
            GameStatePacket state = game->getState();
            PacketHeader h = {(uint32_t)sizeof(state), CMD_GAME_STATE};
            
            sendPacket(s1, &h, sizeof(h));
            sendPacket(s1, &state, sizeof(state));
            sendPacket(s2, &h, sizeof(h));
            sendPacket(s2, &state, sizeof(state));
        } else {
            // If one disconnected, put the other back
            if (s1) matchmakingQueue.push_back(p1.username);
            if (s2) matchmakingQueue.push_back(p2.username);
        }
    }
    
    // 4. Handle Remainder
    for (const auto& entry : pool) {
        matchmakingQueue.push_back(entry.username);
    }
}

std::shared_ptr<asio::ip::tcp::socket> Server::getSocketByUsername(const std::string& username) {
    for (auto& pair : authenticatedUsers) {
        if (pair.second == username) return pair.first;
    }
    return nullptr;
}

std::shared_ptr<GameSession> Server::getGameSession(std::shared_ptr<asio::ip::tcp::socket> client) {
    for (auto& game : activeGames) {
        if (game->getP1Socket() == client || game->getP2Socket() == client) return game;
    }
    return nullptr;
}

void Server::processPacket(std::shared_ptr<asio::ip::tcp::socket> client, PacketHeader& header, const std::vector<char>& body) {
    if (header.command == CMD_REGISTER) {
        if (header.size == sizeof(LoginRequest)) {
            LoginRequest* req = (LoginRequest*)body.data();
            bool success = userManager.registerUser(req->username, req->password);
            PacketHeader resp = {0, success ? (uint8_t)CMD_OK : (uint8_t)CMD_FAIL};
            sendPacket(client, &resp, sizeof(resp));
            if (success) {
                authenticatedUsers[client] = req->username;
                std::cout << "Registered: " << req->username << std::endl;
            }
        }
    } else if (header.command == CMD_LOGIN) {
        if (header.size == sizeof(LoginRequest)) {
            LoginRequest* req = (LoginRequest*)body.data();
            bool success = userManager.loginUser(req->username, req->password);
            PacketHeader resp = {0, success ? (uint8_t)CMD_OK : (uint8_t)CMD_FAIL};
            sendPacket(client, &resp, sizeof(resp));
            if (success) {
                authenticatedUsers[client] = req->username;
                broadcastUserList();
                std::cout << "Logged in: " << req->username << std::endl;
            }
        }
    } else if (header.command == CMD_LIST_USERS) {
        std::string list;
        for (const auto& pair : authenticatedUsers) list += pair.second + "\n";
        PacketHeader resp = {(uint32_t)list.size(), CMD_LIST_USERS_RESP};
        sendPacket(client, &resp, sizeof(resp));
        if (!list.empty()) sendPacket(client, list.c_str(), list.size());
    } else if (header.command == CMD_LEADERBOARD) {
        std::string board = userManager.getLeaderboard();
        PacketHeader resp = {(uint32_t)board.size(), CMD_LEADERBOARD_RESP};
        sendPacket(client, &resp, sizeof(resp));
        if (!board.empty()) sendPacket(client, board.c_str(), board.size());
    } else if (header.command == CMD_CHALLENGE_REQ) {
        if (header.size == sizeof(ChallengePacket)) {
            ChallengePacket* pkt = (ChallengePacket*)body.data();
            std::string target(pkt->targetUser);
            auto targetSock = getSocketByUsername(target);
            if (targetSock) {
                std::string sender = authenticatedUsers[client];
                // Cross challenge check
                if (pendingChallenges.count(target) && pendingChallenges[target] == sender) {
                    // Match
                    pendingChallenges.erase(target);
                    auto game = std::make_shared<GameSession>(target, sender, targetSock, client);
                    activeGames.push_back(game);
                    
                    GameStatePacket state = game->getState();
                    PacketHeader h = {(uint32_t)sizeof(state), CMD_GAME_STATE};
                    
                    sendPacket(targetSock, &h, sizeof(h));
                    sendPacket(targetSock, &state, sizeof(state));
                    sendPacket(client, &h, sizeof(h));
                    sendPacket(client, &state, sizeof(state));
                } else {
                    pendingChallenges[sender] = target;
                    ChallengePacket fwd;
                    strncpy(fwd.targetUser, sender.c_str(), 32);
                    PacketHeader h = {(uint32_t)sizeof(fwd), CMD_CHALLENGE_REQ};
                    sendPacket(targetSock, &h, sizeof(h));
                    sendPacket(targetSock, &fwd, sizeof(fwd));
                }
            }
        }
    } else if (header.command == CMD_CHALLENGE_RESP) {
        if (header.size == sizeof(ChallengePacket)) {
            ChallengePacket* pkt = (ChallengePacket*)body.data();
            std::string origChallenger(pkt->targetUser);
            auto challSock = getSocketByUsername(origChallenger);
            if (challSock) {
                // Accepted
                 auto game = std::make_shared<GameSession>(origChallenger, authenticatedUsers[client], challSock, client);
                 activeGames.push_back(game);
                 
                 GameStatePacket state = game->getState();
                 PacketHeader h = {(uint32_t)sizeof(state), CMD_GAME_STATE};
                 sendPacket(challSock, &h, sizeof(h));
                 sendPacket(challSock, &state, sizeof(state));
                 sendPacket(client, &h, sizeof(h));
                 sendPacket(client, &state, sizeof(state));
            }
        }
    } else if (header.command == CMD_PLAY_AI) {
        if (!getGameSession(client)) {
            std::string p1 = authenticatedUsers[client];
            auto game = std::make_shared<GameSession>(p1, "The Dealer", client, nullptr);
            activeGames.push_back(game);
            
             GameStatePacket state = game->getState();
             PacketHeader h = {(uint32_t)sizeof(state), CMD_GAME_STATE};
             sendPacket(client, &h, sizeof(h));
             sendPacket(client, &state, sizeof(state));
        }
    } else if (header.command == CMD_LIST_REPLAYS) {
        std::string list = ReplayManager::getReplayList(authenticatedUsers[client]);
        PacketHeader resp = {(uint32_t)list.size(), CMD_LIST_REPLAYS_RESP};
        sendPacket(client, &resp, sizeof(resp));
        if(!list.empty()) sendPacket(client, list.c_str(), list.size());
    } else if (header.command == CMD_GET_REPLAY) {
        std::string fname(body.begin(), body.end());
        auto hist = ReplayManager::loadReplay(fname);
        PacketHeader resp = {(uint32_t)(hist.size() * sizeof(GameStatePacket)), CMD_REPLAY_DATA};
        sendPacket(client, &resp, sizeof(resp));
        if(!hist.empty()) sendPacket(client, hist.data(), resp.size);
    } else if (header.command == CMD_GET_HISTORY) {
         auto hist = userManager.getHistory(authenticatedUsers[client]);
         PacketHeader resp = {(uint32_t)(hist.size()*sizeof(HistoryEntry)), CMD_HISTORY_DATA};
         sendPacket(client, &resp, sizeof(resp));
         if(!hist.empty()) sendPacket(client, hist.data(), resp.size);
    } else if (header.command == CMD_RESIGN) {
        auto game = getGameSession(client);
        if (game) {
            game->resign(authenticatedUsers[client]);
            // Broadcast
             GameStatePacket state = game->getState();
             PacketHeader h = {(uint32_t)sizeof(state), CMD_GAME_STATE};
             sendPacket(game->getP1Socket(), &h, sizeof(h));
             sendPacket(game->getP1Socket(), &state, sizeof(state));
             sendPacket(game->getP2Socket(), &h, sizeof(h));
             sendPacket(game->getP2Socket(), &state, sizeof(state));
             
             if (game->isGameOver()) {
                 std::string winner = game->getState().winner;
                 std::string replay = ReplayManager::saveReplay(game->getP1Name(), game->getP2Name(), winner, game->getHistory());
                 userManager.recordMatch(winner, (winner==game->getP1Name())?game->getP2Name():game->getP1Name(), replay);
                 auto it = std::find(activeGames.begin(), activeGames.end(), game);
                 if (it!=activeGames.end()) activeGames.erase(it);
             }
        }
    } else if (header.command == CMD_GAME_MOVE) {
        if (header.size == sizeof(MovePayload)) {
            MovePayload* mv = (MovePayload*)body.data();
            auto game = getGameSession(client);
            if (game) {
                 game->processMove(authenticatedUsers[client], (MoveType)mv->moveType, (ItemType)mv->itemType);
                 // Broadcast
                 GameStatePacket state = game->getState();
                 PacketHeader h = {(uint32_t)sizeof(state), CMD_GAME_STATE};
                 sendPacket(game->getP1Socket(), &h, sizeof(h));
                 sendPacket(game->getP1Socket(), &state, sizeof(state));
                 sendPacket(game->getP2Socket(), &h, sizeof(h));
                 sendPacket(game->getP2Socket(), &state, sizeof(state));
                 
                 if (game->isGameOver()) {
                      std::string winner = game->getState().winner;
                      std::string replay = ReplayManager::saveReplay(game->getP1Name(), game->getP2Name(), winner, game->getHistory());
                      auto deltas = userManager.recordMatch(winner, (winner==game->getP1Name())?game->getP2Name():game->getP1Name(), replay);
                      game->setEloChanges((winner==game->getP1Name())?deltas.first:deltas.second, (winner==game->getP2Name())?deltas.first:deltas.second);
                      
                      // Resend ELO
                        state = game->getState();
                        sendPacket(game->getP1Socket(), &h, sizeof(h));
                        sendPacket(game->getP1Socket(), &state, sizeof(state));
                        sendPacket(game->getP2Socket(), &h, sizeof(h));
                        sendPacket(game->getP2Socket(), &state, sizeof(state));

                      auto it = std::find(activeGames.begin(), activeGames.end(), game);
                      if (it!=activeGames.end()) activeGames.erase(it);
                 }
            }
        }
    } else if (header.command == CMD_QUEUE_JOIN) {
         if (std::find(matchmakingQueue.begin(), matchmakingQueue.end(), authenticatedUsers[client]) == matchmakingQueue.end()) {
             matchmakingQueue.push_back(authenticatedUsers[client]);
         }
         PacketHeader r = {0, CMD_OK};
         sendPacket(client, &r, sizeof(r));
    } else if (header.command == CMD_QUEUE_LEAVE) {
        auto it = std::find(matchmakingQueue.begin(), matchmakingQueue.end(), authenticatedUsers[client]);
        if(it!=matchmakingQueue.end()) matchmakingQueue.erase(it);
        PacketHeader r = {0, CMD_OK};
        sendPacket(client, &r, sizeof(r));
    } else if (header.command == CMD_TOGGLE_PAUSE) {
        auto game = getGameSession(client);
        if (game && game->isAiGame()) {
            game->togglePause();
            GameStatePacket state = game->getState();
            PacketHeader h = {(uint32_t)sizeof(state), CMD_GAME_STATE};
            sendPacket(client, &h, sizeof(h));
            sendPacket(client, &state, sizeof(state));
        }
    } else if (header.command == CMD_FRIEND_ADD) {
        ChallengePacket* pkt = (ChallengePacket*)body.data();
        std::string target = pkt->targetUser;
        if(userManager.addFriendRequest(authenticatedUsers[client], target)) {
             auto ts = getSocketByUsername(target);
             if (ts) {
                 ChallengePacket req; strncpy(req.targetUser, authenticatedUsers[client].c_str(), 32);
                 PacketHeader h = {(uint32_t)sizeof(req), CMD_FRIEND_REQ_INCOMING};
                 sendPacket(ts, &h, sizeof(h));
                 sendPacket(ts, &req, sizeof(req));
             }
        }
    } else if (header.command == CMD_FRIEND_ACCEPT) {
        ChallengePacket* pkt = (ChallengePacket*)body.data();
        userManager.acceptFriendRequest(authenticatedUsers[client], pkt->targetUser);
    } else if (header.command == CMD_FRIEND_REMOVE) {
        ChallengePacket* pkt = (ChallengePacket*)body.data();
        userManager.removeFriend(authenticatedUsers[client], pkt->targetUser);
    } else if (header.command == CMD_FRIEND_LIST) {
        std::string user = authenticatedUsers[client];
        std::string list = userManager.getFriendList(user);
        // Inject online
        std::stringstream ss(list);
        std::string item, finalList;
        while(std::getline(ss, item, ',')) {
            if(item.empty()) continue;
            auto colon = item.find(':');
            if(colon != std::string::npos) {
                std::string fname = item.substr(0, colon);
                std::string stat = item.substr(colon+1);
                if(stat == "ACCEPTED") {
                     stat = getSocketByUsername(fname) ? "ONLINE" : "OFFLINE";
                }
                if(!finalList.empty()) finalList += ",";
                finalList += fname + ":" + stat;
            }
        }
        PacketHeader resp = {(uint32_t)finalList.size(), CMD_FRIEND_LIST_RESP};
        sendPacket(client, &resp, sizeof(resp));
        if(!finalList.empty()) sendPacket(client, finalList.c_str(), finalList.size());
    }
}

void Server::broadcastUserList() {
    std::string list;
    for (auto& pair : authenticatedUsers) list += pair.second + "\n";
    PacketHeader resp = {(uint32_t)list.size(), CMD_LIST_USERS_RESP};
    for (auto& pair : authenticatedUsers) {
        sendPacket(pair.first, &resp, sizeof(resp));
        if(!list.empty()) sendPacket(pair.first, list.c_str(), list.size());
    }
}

}
