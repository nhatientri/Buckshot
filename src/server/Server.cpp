#include "Server.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <algorithm>
#include <sstream>
#include "../common/Protocol.h"
#include "ReplayManager.h"
#include <chrono>

namespace Buckshot {

Server::Server(int port) : port(port), running(false), serverSocket(-1) {
    // userManager.loadUsers(); // Migration happens in constructor now
    lastTimeoutCheck = std::chrono::steady_clock::now();
}

void Server::run() {
    setupSocket();
    running = true;

    fd_set readfds;
    std::cout << "Server listening on port " << port << "..." << std::endl;

    while (running) {
        FD_ZERO(&readfds);
        FD_SET(serverSocket, &readfds);
        int max_sd = serverSocket;

        for (int sd : clientSockets) {
            if (sd > 0) FD_SET(sd, &readfds);
            if (sd > max_sd) max_sd = sd;
        }

        // Timeout Check
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastTimeoutCheck).count() >= 1) {
             lastTimeoutCheck = now;
             for (auto it = activeGames.begin(); it != activeGames.end();) {
                 auto& game = *it;
                 if (game->checkTimeout(30)) { // 30s timeout
                     std::cout << "Game timed out!" << std::endl;
                     // Broadcast Game Over
                     GameStatePacket state = game->getState();
                     PacketHeader stateHead;
                     stateHead.command = CMD_GAME_STATE;
                     stateHead.size = sizeof(GameStatePacket);
                     
                     send(game->getP1Fd(), &stateHead, sizeof(stateHead), 0);
                     send(game->getP1Fd(), &state, sizeof(state), 0);
                     send(game->getP2Fd(), &stateHead, sizeof(stateHead), 0);
                     send(game->getP2Fd(), &state, sizeof(state), 0);
                     
                     // Record and cleanup
                      std::string winner = game->getState().winner; 
                      std::string p1n = game->getP1Name();
                      std::string p2n = game->getP2Name();
                      std::string loser = (winner == p1n) ? p2n : p1n;
                      

                      
                      std::string replayFile = ReplayManager::saveReplay(p1n, p2n, winner, game->getHistory());
                      auto deltas = userManager.recordMatch(winner, loser, replayFile);

                      int p1Delta = (winner == p1n) ? deltas.first : deltas.second;
                      int p2Delta = (winner == p2n) ? deltas.first : deltas.second;
                      game->setEloChanges(p1Delta, p2Delta);
                      
                      // Re-broadcast with Elo
                      state = game->getState();
                      send(game->getP1Fd(), &stateHead, sizeof(stateHead), 0);
                      send(game->getP1Fd(), &state, sizeof(state), 0);
                      send(game->getP2Fd(), &stateHead, sizeof(stateHead), 0);
                      send(game->getP2Fd(), &state, sizeof(state), 0);

                     it = activeGames.erase(it);
                 } else {
                     ++it;
                 }
             }
        }

        // AI Logic Loop
        for (auto it = activeGames.begin(); it != activeGames.end();) {
            auto& game = *it;
            if (game->isAiGame() && game->executeAiTurn()) {
                 // Broadcast State
                 GameStatePacket state = game->getState();
                 PacketHeader stateHead;
                 stateHead.command = CMD_GAME_STATE;
                 stateHead.size = sizeof(GameStatePacket);
                 
                 // AI is P2, so P1 is the human
                 if (game->getP1Fd() != -1) {
                     send(game->getP1Fd(), &stateHead, sizeof(stateHead), 0);
                     send(game->getP1Fd(), &state, sizeof(state), 0);
                 }
                 
                 if (game->isGameOver()) {
                     std::string winner = game->getState().winner; 
                     std::string p1n = game->getP1Name();
                     std::string p2n = game->getP2Name();
                     std::string loser = (winner == p1n) ? p2n : p1n;
                     
                     // Handle AI game record? userManager might return 0 for "The Dealer"

                      
                      // Handle AI game record? userManager might return 0 for "The Dealer"
                      std::string replayFile = ReplayManager::saveReplay(p1n, p2n, winner, game->getHistory());
                      userManager.recordMatch(winner, loser, replayFile);
                     
                     it = activeGames.erase(it);
                     continue;
                 }
            }
            ++it;
        }

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int activity = select(max_sd + 1, &readfds, nullptr, nullptr, &tv);

        if ((activity < 0) && (errno != EINTR)) {
            std::cerr << "Select error" << std::endl;
        }

        if (FD_ISSET(serverSocket, &readfds)) {
            handleNewConnection();
        }

        handleClientActivity(readfds);
        
        // Matchmaking
        processMatchmaking();
    }
}

void Server::setupSocket() {
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(serverSocket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(serverSocket, 3) < 0) {
        perror("Listen");
        exit(EXIT_FAILURE);
    }
}

void Server::handleNewConnection() {
    sockaddr_in address;
    int addrlen = sizeof(address);
    int new_socket = accept(serverSocket, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    
    if (new_socket < 0) {
        perror("Accept");
        return;
    }

    std::cout << "New connection, socket fd is " << new_socket << std::endl;
    clientSockets.push_back(new_socket);
}

void Server::handleClientActivity(fd_set& readfds) {
    for (auto it = clientSockets.begin(); it != clientSockets.end();) {
        int sd = *it;
        if (FD_ISSET(sd, &readfds)) {
            PacketHeader header;
            int valread = read(sd, &header, sizeof(PacketHeader));
            
            if (valread == 0) {
                // Disconnected logic
                auto game = getGameSession(sd);
                if (game) {
                    std::string user = authenticatedUsers[sd];
                    std::cout << "Player " << user << " disconnected during game." << std::endl;
                    game->resign(user);
                    
                    // Notify other player
                    GameStatePacket state = game->getState();
                    PacketHeader stateHead;
                    stateHead.command = CMD_GAME_STATE;
                    stateHead.size = sizeof(GameStatePacket);
                    
                    int p1 = game->getP1Fd();
                    int p2 = game->getP2Fd();
                    int otherFd = (sd == p1) ? p2 : p1;
                    
                    // Only send to the connected player
                    send(otherFd, &stateHead, sizeof(stateHead), 0);
                    send(otherFd, &state, sizeof(state), 0);
                    
                    if (game->isGameOver()) {
                        std::string winner = game->getState().winner; 
                        std::string p1n = game->getP1Name();
                        std::string p2n = game->getP2Name();
                        std::string loser = (winner == p1n) ? p2n : p1n;
                        

                        
                        std::string replayFile = ReplayManager::saveReplay(p1n, p2n, winner, game->getHistory());
                        userManager.recordMatch(winner, loser, replayFile);
                        
                        auto gIt = std::find(activeGames.begin(), activeGames.end(), game);
                        if (gIt != activeGames.end()) {
                            activeGames.erase(gIt);
                        }
                    }
                }

                // Disconnected cleanup
                close(sd);
                // Clean pending challenges
                if (authenticatedUsers.count(sd)) {
                    std::string u = authenticatedUsers[sd];
                    pendingChallenges.erase(u);
                    // Also remove any where u is the target? 
                    // To do strictly: iterate map. 
                    for (auto it = pendingChallenges.begin(); it != pendingChallenges.end(); ) {
                        if (it->second == u) it = pendingChallenges.erase(it);
                        else ++it;
                    }
                    
                    // Remove from Matchmaking Queue
                    auto qIt = std::find(matchmakingQueue.begin(), matchmakingQueue.end(), u);
                    if (qIt != matchmakingQueue.end()) {
                         matchmakingQueue.erase(qIt);
                    }
                }

                authenticatedUsers.erase(sd);
                broadcastUserList(); // Update others
                it = clientSockets.erase(it);
                continue;
            } else if (valread == sizeof(PacketHeader)) {
                // Read body
                std::vector<char> body(header.size);
                int bodyRead = 0;
                if (header.size > 0) {
                     bodyRead = read(sd, body.data(), header.size);
                }
                
                if (bodyRead == header.size) {
                    processPacket(sd, (char*)&header, sizeof(PacketHeader)); // Pass just header for info? Or pass payload?
                    // Better: processPacket takes command and payload
                    if (header.command == CMD_REGISTER) {
                        if (header.size == sizeof(LoginRequest)) {
                            LoginRequest* req = (LoginRequest*)body.data();
                            bool success = userManager.registerUser(req->username, req->password);
                            PacketHeader resp;
                            resp.command = success ? CMD_OK : CMD_FAIL;
                            resp.size = 0; 
                            send(sd, &resp, sizeof(resp), 0);
                            if (success) {
                                authenticatedUsers[sd] = req->username;
                            }
                            std::cout << "Register request: " << req->username << " -> " << success << std::endl;
                        }
                    } else if (header.command == CMD_LOGIN) {
                        if (header.size == sizeof(LoginRequest)) {
                            LoginRequest* req = (LoginRequest*)body.data();
                            bool success = userManager.loginUser(req->username, req->password);
                            PacketHeader resp;
                            resp.command = success ? CMD_OK : CMD_FAIL;
                            resp.size = 0;
                            send(sd, &resp, sizeof(resp), 0);
                            if (success) {
                                authenticatedUsers[sd] = req->username;
                                broadcastUserList();
                            }
                            std::cout << "Login request: " << req->username << " -> " << success << std::endl;
                        }
                    } else if (header.command == CMD_LIST_USERS) {
                        std::string userList;
                        for (const auto& pair : authenticatedUsers) {
                            userList += pair.second + "\n";
                        }
                        PacketHeader resp;
                        resp.command = CMD_LIST_USERS_RESP;
                        resp.size = userList.size();
                        send(sd, &resp, sizeof(resp), 0);
                        if (resp.size > 0) {
                            send(sd, userList.c_str(), userList.size(), 0);
                        }
                    } else if (header.command == CMD_LEADERBOARD) {
                        std::string board = userManager.getLeaderboard();
                        PacketHeader resp;
                        resp.command = CMD_LEADERBOARD_RESP;
                        resp.size = board.size();
                        send(sd, &resp, sizeof(resp), 0);
                        if (resp.size > 0) {
                            send(sd, board.c_str(), board.size(), 0);
                        }
                    } else if (header.command == CMD_CHALLENGE_REQ) {
                        if (header.size == sizeof(ChallengePacket)) {
                            ChallengePacket* pkt = (ChallengePacket*)body.data();
                            std::string target(pkt->targetUser);
                            
                            // Find target fd
                            int targetFd = -1;
                            for (const auto& pair : authenticatedUsers) {
                                if (pair.second == target) {
                                    targetFd = pair.first;
                                    break;
                                }
                            }

                            if (targetFd != -1) {
                                std::string sender = authenticatedUsers[sd];

                                // Check if this is a Cross-Challenge (Target already challenged Sender)
                                if (pendingChallenges.count(target) && pendingChallenges[target] == sender) {
                                    // MATCH FOUND! START GAME
                                    std::cout << "Cross-Challenge matched: " << sender << " vs " << target << std::endl;
                                    
                                    pendingChallenges.erase(target); 
                                    pendingChallenges.erase(sender); // Just in case

                                    auto newGame = std::make_shared<GameSession>(target, sender, targetFd, sd);
                                    activeGames.push_back(newGame);
                                    
                                    GameStatePacket state = newGame->getState();
                                    PacketHeader stateHead;
                                    stateHead.command = CMD_GAME_STATE;
                                    stateHead.size = sizeof(GameStatePacket);
                                    
                                    send(targetFd, &stateHead, sizeof(stateHead), 0);
                                    send(targetFd, &state, sizeof(state), 0);
                                    
                                    send(sd, &stateHead, sizeof(stateHead), 0);
                                    send(sd, &state, sizeof(state), 0);
                                    
                                } else {
                                    // Normal Challenge
                                    pendingChallenges[sender] = target;
                                    
                                    // Self check
                                    if (target == sender) {
                                         // Just ignore
                                    } else {
                                        ChallengePacket forwardPkt;
                                        strncpy(forwardPkt.targetUser, sender.c_str(), 32);

                                        PacketHeader hdr = {(uint32_t)sizeof(ChallengePacket), CMD_CHALLENGE_REQ};
                                        
                                        send(targetFd, &hdr, sizeof(hdr), 0);
                                        send(targetFd, &forwardPkt, sizeof(forwardPkt), 0);
                                        std::cout << "Forwarded challenge from " << sender << " to " << target << " (" << targetFd << ")" << std::endl;
                                    }
                                }
                            } else {
                                // User not found
                                std::cout << "User " << target << " not found for challenge." << std::endl;
                            }
                        }
                    } else if (header.command == CMD_CHALLENGE_RESP) {
                         if (header.size == sizeof(ChallengePacket)) {
                            ChallengePacket* pkt = (ChallengePacket*)body.data();
                            std::string originalChallenger(pkt->targetUser); // The user who sent the original challenge
                            // In this simplified flow, success/fail is implied by the response code logic I haven't fully specced yet. 
                            // Actually, I should probably use a Response packet or just forward the Resp.
                            // Let's assume the body contains the "Target's decision" + "Target's Name"? 
                            // Or better: ClientB sends "Accept" to Server. Server knows ClientB matches with ClientA? 
                            // Stateless is harder. Let's send the "Challenger Name" back in the packet so Server knows who to notify.
                            
                            int challengerFd = -1;
                             for (const auto& pair : authenticatedUsers) {
                                if (pair.second == originalChallenger) {
                                    challengerFd = pair.first;
                                    break;
                                }
                            }
                            
                            if (challengerFd != -1) {
                                // Forward the response to the challenger
                                // We can re-use CMD_CHALLENGE_RESP
                                // Ideally we need to know if it was ACCEPT or DECLINE. 
                                // I'll add a 'responseCode' to the packet or just use a generic 'RESPONSE' logic?
                                // Let's reuse CMD_CHALLENGE_RESP for "ACCEPTED". 
                                // And send a different packet for "DECLINED" or add a field.
                                // Quick fix: Add 'responseCode' to Protocol if not exists. I see `ResponseCode` enum but where is it used?
                                // I will check Protocol.h ResponseCode enum usage.
                                // For now, let's assume if I send CMD_CHALLENGE_RESP back, it means ACCEPTED.
                                
                                PacketHeader respHeader;
                                respHeader.command = CMD_CHALLENGE_RESP; // ACCEPTED
                                respHeader.size = sizeof(ChallengePacket);
                                
                                ChallengePacket respPkt;
                                strncpy(respPkt.targetUser, authenticatedUsers[sd].c_str(), 32); // Who accepted (ClientB)
                                
                                send(challengerFd, &respHeader, sizeof(respHeader), 0);
                                send(challengerFd, &respPkt, sizeof(respPkt), 0);
                                
                                std::cout << "Challenge accepted by " << authenticatedUsers[sd] << ", notifying " << originalChallenger << std::endl;
                                
                                // START GAME
                                auto newGame = std::make_shared<GameSession>(originalChallenger, authenticatedUsers[sd], challengerFd, sd);
                                activeGames.push_back(newGame);
                                
                                // Send initial state
                                GameStatePacket state = newGame->getState();
                                PacketHeader stateHead;
                                stateHead.command = CMD_GAME_STATE;
                                stateHead.size = sizeof(GameStatePacket);
                                
                                send(challengerFd, &stateHead, sizeof(stateHead), 0);
                                send(challengerFd, &state, sizeof(state), 0);
                                
                                send(sd, &stateHead, sizeof(stateHead), 0);
                                send(sd, &state, sizeof(state), 0);

                            }
                          }
                     } else if (header.command == CMD_PLAY_AI) {
                         // Check if already in game
                         if (getGameSession(sd) != nullptr) {
                             std::cout << "User " << authenticatedUsers[sd] << " already in game. Ignoring PlayAi request." << std::endl;
                         } else {
                             // Start game
                             std::string p1Name = authenticatedUsers[sd];
                             std::string p2Name = "The Dealer";
                             std::cout << "Starting AI Game for " << p1Name << std::endl;
                             auto newGame = std::make_shared<GameSession>(p1Name, p2Name, sd, -1);
                             activeGames.push_back(newGame);
                             
                             GameStatePacket state = newGame->getState();
                             PacketHeader stateHead;
                             stateHead.command = CMD_GAME_STATE;
                             stateHead.size = sizeof(GameStatePacket);
                             send(sd, &stateHead, sizeof(stateHead), 0);
                             send(sd, &state, sizeof(state), 0);
                         }
                         std::string p1Name = authenticatedUsers[sd];
                         std::string p2Name = "The Dealer";
                         std::cout << "Starting AI Game for " << p1Name << std::endl;
                         auto newGame = std::make_shared<GameSession>(p1Name, p2Name, sd, -1);
                         activeGames.push_back(newGame);
                         
                         GameStatePacket state = newGame->getState();
                         PacketHeader stateHead;
                         stateHead.command = CMD_GAME_STATE;
                         stateHead.size = sizeof(GameStatePacket);
                         send(sd, &stateHead, sizeof(stateHead), 0);
                         send(sd, &state, sizeof(state), 0);

                     } else if (header.command == CMD_LIST_REPLAYS) {
                        std::string user = authenticatedUsers[sd];
                        std::string list = ReplayManager::getReplayList(user);
                        PacketHeader resp;
                        resp.command = CMD_LIST_REPLAYS_RESP;
                        resp.size = list.size();
                        send(sd, &resp, sizeof(resp), 0);
                        if (list.size() > 0) send(sd, list.c_str(), list.size(), 0);
                    } else if (header.command == CMD_GET_REPLAY) {
                        if (header.size > 0) {
                            std::string filename(body.begin(), body.end());
                            auto history = ReplayManager::loadReplay(filename);
                            
                            PacketHeader resp;
                            resp.command = CMD_REPLAY_DATA;
                            resp.size = history.size() * sizeof(GameStatePacket);
                            send(sd, &resp, sizeof(resp), 0);
                            if (resp.size > 0) send(sd, history.data(), resp.size, 0);
                        }

                    } else if (header.command == CMD_GET_HISTORY) {
                         std::string user = authenticatedUsers[sd];
                         std::vector<HistoryEntry> history = userManager.getHistory(user);
                         
                         PacketHeader resp;
                         resp.command = CMD_HISTORY_DATA;
                         resp.size = history.size() * sizeof(HistoryEntry);
                         send(sd, &resp, sizeof(resp), 0);
                         if (resp.size > 0) {
                             send(sd, history.data(), resp.size, 0);
                         }
                    } else if (header.command == CMD_RESIGN) {
                         auto game = getGameSession(sd);
                         if (game) {
                             std::string user = authenticatedUsers[sd];
                             game->resign(user);
                              
                             // Broadcast State & Game Over
                             GameStatePacket state = game->getState();
                             PacketHeader stateHead;
                             stateHead.command = CMD_GAME_STATE;
                             stateHead.size = sizeof(GameStatePacket);
                             
                             send(game->getP1Fd(), &stateHead, sizeof(stateHead), 0);
                             send(game->getP1Fd(), &state, sizeof(state), 0);
                             send(game->getP2Fd(), &stateHead, sizeof(stateHead), 0);
                             send(game->getP2Fd(), &state, sizeof(state), 0);
                             
                             if (game->isGameOver()) {
                                 // Record Match Result
                                 std::string winner = game->getState().winner; 
                                 std::string p1 = game->getP1Name();
                                 std::string p2 = game->getP2Name();
                                 std::string loser = (winner == p1) ? p2 : p1;
                                 

                                 
                                 std::string replayFile = ReplayManager::saveReplay(game->getP1Name(), game->getP2Name(), winner, game->getHistory());
                                 userManager.recordMatch(winner, loser, replayFile);

                                  auto it = std::find(activeGames.begin(), activeGames.end(), game);
                                  if (it != activeGames.end()) {
                                      activeGames.erase(it);
                                  }
                             }
                         }
                    } else if (header.command == CMD_GAME_MOVE) {
                        if (header.size == sizeof(MovePayload)) {
                            MovePayload* mv = (MovePayload*)body.data();
                            auto game = getGameSession(sd);
                            if (game) {
                                std::string user = authenticatedUsers[sd];
                                game->processMove(user, (MoveType)mv->moveType, (ItemType)mv->itemType);
                                
                                // Broadcast State
                                GameStatePacket state = game->getState();
                                PacketHeader stateHead;
                                stateHead.command = CMD_GAME_STATE;
                                stateHead.size = sizeof(GameStatePacket);
                                
                                send(game->getP1Fd(), &stateHead, sizeof(stateHead), 0);
                                send(game->getP1Fd(), &state, sizeof(state), 0);
                                send(game->getP2Fd(), &stateHead, sizeof(stateHead), 0);
                                send(game->getP2Fd(), &state, sizeof(state), 0);
                                
                                if (game->isGameOver()) {
                                    // Record Match Result
                                    std::string winner = game->getState().winner; 
                                    std::string p1 = game->getP1Name();
                                    std::string p2 = game->getP2Name();
                                    std::string loser = (winner == p1) ? p2 : p1;
                                    

                                    
                                    // SAVE REPLAY
                                    std::string replayFile = ReplayManager::saveReplay(game->getP1Name(), game->getP2Name(), winner, game->getHistory());

                                    auto deltas = userManager.recordMatch(winner, loser, replayFile);
                                    int p1Delta = (winner == p1) ? deltas.first : deltas.second;
                                    int p2Delta = (winner == p2) ? deltas.first : deltas.second;
                                    game->setEloChanges(p1Delta, p2Delta);

                                    // Remove game from active list? 
                                    // For now, let's just keep it (memory leak in long run but okay for demo) 
                                    // OR remove it: 
                                    // But we just sent the "Game Over" packet. Clients might still be reading?
                                    // Clients just print "Game Over" and return to loop.
                                    // Safe to remove.
                                     auto it = std::find(activeGames.begin(), activeGames.end(), game);
                                     if (it != activeGames.end()) {
                                         activeGames.erase(it);
                                     }
                                }
                            }
                        }
    } else if (header.command == CMD_QUEUE_JOIN) {
                        std::string user = authenticatedUsers[sd];
                        // Check if already in queue
                        if (std::find(matchmakingQueue.begin(), matchmakingQueue.end(), user) == matchmakingQueue.end()) {
                            matchmakingQueue.push_back(user);
                            std::cout << user << " joined matchmaking queue." << std::endl;
                        }
                        // Send OK
                        PacketHeader resp;
                        resp.command = CMD_OK; 
                        resp.size = 0;
                        send(sd, &resp, sizeof(resp), 0);
                        
                     } else if (header.command == CMD_QUEUE_LEAVE) {
                         std::string user = authenticatedUsers[sd];
                         auto it = std::find(matchmakingQueue.begin(), matchmakingQueue.end(), user);
                         if (it != matchmakingQueue.end()) {
                             matchmakingQueue.erase(it);
                             std::cout << user << " left matchmaking queue." << std::endl;
                         }
                         PacketHeader resp;
                         resp.command = CMD_OK;
                         resp.size = 0;
                         send(sd, &resp, sizeof(resp), 0);
                    } else if (header.command == CMD_TOGGLE_PAUSE) {
                         auto game = getGameSession(sd);
                         if (game && game->isAiGame()) {
                             game->togglePause(); // Toggle logic
                             
                             // Broadcast new state
                             GameStatePacket state = game->getState();
                             PacketHeader stateHead;
                             stateHead.command = CMD_GAME_STATE;
                             stateHead.size = sizeof(GameStatePacket);
                             
                             // Send to player (and AI technically, but AI is internal)
                             send(sd, &stateHead, sizeof(stateHead), 0);
                             send(sd, &state, sizeof(state), 0);
                         }
                    } else if (header.command == CMD_FRIEND_ADD) {
                        if (header.size == sizeof(ChallengePacket)) {
                            ChallengePacket* pkt = (ChallengePacket*)body.data();
                            std::string target(pkt->targetUser);
                            std::string user = authenticatedUsers[sd];
                            bool ok = userManager.addFriendRequest(user, target);
                            if (ok) {
                                 int targetFd = -1;
                                 for (const auto& pair : authenticatedUsers) {
                                     if (pair.second == target) {
                                         targetFd = pair.first;
                                         break;
                                     }
                                 }
                                 if (targetFd != -1) {
                                     ChallengePacket reqPkt;
                                     strncpy(reqPkt.targetUser, user.c_str(), 32); 
                                     PacketHeader h = {(uint32_t)sizeof(ChallengePacket), CMD_FRIEND_REQ_INCOMING};
                                     send(targetFd, &h, sizeof(h), 0);
                                     send(targetFd, &reqPkt, sizeof(reqPkt), 0);
                                 }
                            }
                        }
                    } else if (header.command == CMD_FRIEND_ACCEPT) {
                        if (header.size == sizeof(ChallengePacket)) {
                            ChallengePacket* pkt = (ChallengePacket*)body.data();
                            std::string target(pkt->targetUser); 
                            std::string user = authenticatedUsers[sd];
                            userManager.acceptFriendRequest(user, target);
                        }
                    } else if (header.command == CMD_FRIEND_REMOVE) {
                        if (header.size == sizeof(ChallengePacket)) {
                            ChallengePacket* pkt = (ChallengePacket*)body.data();
                            std::string target(pkt->targetUser);
                            std::string user = authenticatedUsers[sd];
                            userManager.removeFriend(user, target);
                        }
                    } else if (header.command == CMD_FRIEND_LIST) {
                        std::string user = authenticatedUsers[sd];
                        std::string rawList = userManager.getFriendList(user);
                        
                        // Parse and inject Online status
                        std::string finalList;
                        std::stringstream ss(rawList);
                        std::string item;
                        while (std::getline(ss, item, ',')) {
                            if (item.empty()) continue;
                            size_t colon = item.find(':');
                            if (colon != std::string::npos) {
                                std::string fName = item.substr(0, colon);
                                std::string status = item.substr(colon+1);
                                
                                if (status == "ACCEPTED") {
                                    // Check if online
                                    bool isOnline = false;
                                    for(const auto& pair : authenticatedUsers) {
                                        if (pair.second == fName) {
                                            isOnline = true;
                                            break;
                                        }
                                    }
                                    status = isOnline ? "ONLINE" : "OFFLINE";
                                }
                                
                                if (!finalList.empty()) finalList += ",";
                                finalList += fName + ":" + status;
                            }
                        }
                        
                        PacketHeader resp;
                        resp.command = CMD_FRIEND_LIST_RESP;
                        resp.size = finalList.size();
                        send(sd, &resp, sizeof(resp), 0);
                        if (finalList.size() > 0) send(sd, finalList.c_str(), finalList.size(), 0);
                    }
                }
            } else {
                // Malformed
            }
        }
        ++it;
    }
}

void Server::processMatchmaking() {
    // Simple FIFO matching for now
    // Challenge: Check ELO difference?
    // For now, just match the first 2 people.
    
    while (matchmakingQueue.size() >= 2) {
        std::string user1 = matchmakingQueue[0];
        std::string user2 = matchmakingQueue[1];
        
        // Remove from queue
        matchmakingQueue.erase(matchmakingQueue.begin());
        matchmakingQueue.erase(matchmakingQueue.begin());
        
        // Find FDs
        int fd1 = -1, fd2 = -1;
        for (const auto& pair : authenticatedUsers) {
            if (pair.second == user1) fd1 = pair.first;
            if (pair.second == user2) fd2 = pair.first;
        }
        
        // Verify connectivity
        if (fd1 == -1 || fd2 == -1) {
            // One user lost connection?
            if (fd1 != -1) matchmakingQueue.push_back(user1); // Put back
            if (fd2 != -1) matchmakingQueue.push_back(user2); // Put back
            continue;
        }
        
        std::cout << "Matchmaking: Paired " << user1 << " vs " << user2 << std::endl;
        
        // Start Game
        auto newGame = std::make_shared<GameSession>(user1, user2, fd1, fd2);
        activeGames.push_back(newGame);
        
        // Broadcast Initial State
        GameStatePacket state = newGame->getState();
        PacketHeader stateHead;
        stateHead.command = CMD_GAME_STATE;
        stateHead.size = sizeof(GameStatePacket);
        
        send(fd1, &stateHead, sizeof(stateHead), 0);
        send(fd1, &state, sizeof(state), 0);
        
        send(fd2, &stateHead, sizeof(stateHead), 0);
        send(fd2, &state, sizeof(state), 0);
    }
}

void Server::processPacket(int clientFd, const char* buffer, int size) {
    // Unused separate function
}

std::shared_ptr<GameSession> Server::getGameSession(int fd) {
    for (auto& game : activeGames) {
        if (game->getP1Fd() == fd || game->getP2Fd() == fd) {
            return game;
        }
    }
    return nullptr;
}

void Server::broadcastUserList() {
    std::string userList;
    for (const auto& pair : authenticatedUsers) {
        userList += pair.second + "\n";
    }
    
    PacketHeader resp;
    resp.command = CMD_LIST_USERS_RESP;
    resp.size = userList.size();
    
    for (const auto& pair : authenticatedUsers) {
        int fd = pair.first;
        send(fd, &resp, sizeof(resp), 0);
        if (resp.size > 0) {
            send(fd, userList.c_str(), userList.size(), 0);
        }
    }
}

}
