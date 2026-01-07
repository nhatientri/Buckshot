#include "Server.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <algorithm>
#include "../common/Protocol.h"

namespace Buckshot {

Server::Server(int port) : port(port), running(false), serverSocket(-1) {
    userManager.loadUsers();
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

        int activity = select(max_sd + 1, &readfds, nullptr, nullptr, nullptr);

        if ((activity < 0) && (errno != EINTR)) {
            std::cerr << "Select error" << std::endl;
        }

        if (FD_ISSET(serverSocket, &readfds)) {
            handleNewConnection();
        }

        handleClientActivity(readfds);
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
                // Disconnected
                close(sd);
                std::cout << "Host disconnected, fd " << sd << std::endl;
                authenticatedUsers.erase(sd);
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
                            resp.command = success ? CMD_LOGIN_SUCCESS : CMD_LOGIN_FAIL;
                            resp.size = 0; // No body for now, or maybe message
                            send(sd, &resp, sizeof(resp), 0);
                            std::cout << "Register request: " << req->username << " -> " << success << std::endl;
                        }
                    } else if (header.command == CMD_LOGIN) {
                        if (header.size == sizeof(LoginRequest)) {
                            LoginRequest* req = (LoginRequest*)body.data();
                            bool success = userManager.loginUser(req->username, req->password);
                            PacketHeader resp;
                            resp.command = success ? CMD_LOGIN_SUCCESS : CMD_LOGIN_FAIL;
                            resp.size = 0;
                            send(sd, &resp, sizeof(resp), 0);
                            if (success) {
                                authenticatedUsers[sd] = req->username;
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
                                // Found, forward request
                                // We need to send the Challenger's name to the Target
                                ChallengePacket forwardPkt;
                                std::string challenger = authenticatedUsers[sd];
                                strncpy(forwardPkt.targetUser, challenger.c_str(), 32);

                                PacketHeader fwdHeader;
                                fwdHeader.command = CMD_CHALLENGE_REQ;
                                fwdHeader.size = sizeof(ChallengePacket);
                                
                                send(targetFd, &fwdHeader, sizeof(fwdHeader), 0);
                                send(targetFd, &forwardPkt, sizeof(forwardPkt), 0);
                                std::cout << "Forwarding challenge from " << challenger << " to " << target << std::endl;
                            } else {
                                // Not found, send fail ? (TODO)
                                std::cout << "Challenge target not found: " << target << std::endl;
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
                                    
                                    userManager.recordMatch(winner, loser);
                                    
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
                    }
                }
            } else {
                // Malformed
            }
        }
        ++it;
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

}
