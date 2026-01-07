#include "Client.h"
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include "../common/Protocol.h"
#include <sstream>

namespace Buckshot {

bool Client::connectToServer(const std::string& ip, int port) {
    socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd < 0) {
        std::cerr << "Socket creation error" << std::endl;
        return false;
    }

    sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported" << std::endl;
        return false;
    }

    if (connect(socketFd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Connection Failed" << std::endl;
        return false;
    }

    return true;
}

void Client::run() {
    running = true;
    // Simple receive thread
    std::thread rectThread([this]() {
        while (running) {
            PacketHeader header;
            int valread = read(socketFd, &header, sizeof(PacketHeader));
            if (valread == sizeof(PacketHeader)) {
                if (header.command == CMD_LOGIN_SUCCESS) {
                    std::cout << "[Server] Login/Register Successful!" << std::endl;
                } else if (header.command == CMD_LOGIN_FAIL) {
                    std::cout << "[Server] Login/Register Failed!" << std::endl;
                } else if (header.command == CMD_LIST_USERS_RESP) {
                    if (header.size > 0) {
                        std::vector<char> listBody(header.size + 1);
                        int r = read(socketFd, listBody.data(), header.size);
                        listBody[r] = '\0';
                        std::cout << "[Server] Online Users:\n" << listBody.data() << std::endl;
                    } else {
                        std::cout << "[Server] No other users online." << std::endl;
                    }
                } else if (header.command == CMD_CHALLENGE_REQ) {
                    // Incoming challenge
                    if (header.size == sizeof(ChallengePacket)) {
                        ChallengePacket pkt;
                        read(socketFd, &pkt, sizeof(pkt));
                        std::cout << "[Server] You have been CHALLENGED by: " << pkt.targetUser << "\nType 'ACCEPT " << pkt.targetUser << "' to start!" << std::endl;
                    }
                } else if (header.command == CMD_CHALLENGE_RESP) {
                    // Challenge Accepted by opponent
                    if (header.size == sizeof(ChallengePacket)) {
                        ChallengePacket pkt;
                        read(socketFd, &pkt, sizeof(pkt));
                        std::cout << "[Server] " << pkt.targetUser << " ACCEPTED your challenge! Game Start!" << std::endl;
                    }
                } else if (header.command == CMD_GAME_STATE) {
                     if (header.size == sizeof(GameStatePacket)) {
                        GameStatePacket top;
                        read(socketFd, &top, sizeof(top));
                        
                        std::cout << "\n========================================\n";
                        if (top.gameOver) {
                             std::cout << "GAME OVER! Winner: " << top.winner << "\n";
                        } else {
                            std::cout << "Turn: " << top.currentTurnUser << "\n";
                            std::cout << "Shells: " << top.shellsRemaining << " | P1 HP: " << top.p1Hp << " | P2 HP: " << top.p2Hp << "\n";
                            
                            auto printInventory = [](const char* label, uint8_t* inv) {
                                std::cout << label << ": ";
                                for(int i=0; i<8; ++i) {
                                    if (inv[i] != 0) {
                                        switch(inv[i]) {
                                            case 1: std::cout << "[BEER] "; break;
                                            case 2: std::cout << "[CIGS] "; break;
                                            case 3: std::cout << "[CUFFS] "; break;
                                            case 4: std::cout << "[GLASS] "; break;
                                            case 5: std::cout << "[KNIFE] "; break;
                                            case 6: std::cout << "[INVERT] "; break;
                                            case 7: std::cout << "[MEDS] "; break;
                                        }
                                    }
                                }
                                std::cout << "\n";
                            };
                            
                            printInventory("P1 Items", top.p1Inventory);
                            printInventory("P2 Items", top.p2Inventory);
                            
                            if (top.knifeActive) std::cout << "WARNING: Knife is Active! (Double Damage)\n";
                            if (top.p1Handcuffed) std::cout << "P1 is Handcuffed!\n";
                            if (top.p2Handcuffed) std::cout << "P2 is Handcuffed!\n";
                        }
                        std::cout << "Event: " << top.message << "\n";
                        std::cout << "========================================\n" << std::endl;
                     }
                } else if (header.command == CMD_LEADERBOARD_RESP) {
                    if (header.size > 0) {
                        std::vector<char> buff(header.size + 1);
                        int r = read(socketFd, buff.data(), header.size);
                        buff[r] = '\0';
                        std::cout << buff.data() << std::endl;
                    }
                } else {
                     std::cout << "[Server] Unknown command: " << (int)header.command << std::endl;
                }
            } else if (valread <= 0) {
                 std::cout << "Server disconnected" << std::endl;
                 running = false;
                 break;
            }
        }
    });
    
    rectThread.detach();

    std::cout << "Connected! Commands:\n REGISTER <user> <pass>\n LOGIN <user> <pass>\n LIST\n LEADERBOARD\n CHALLENGE <user>\n ACCEPT <user>\n SHOOT <SELF|OPPONENT>\n USE <ITEM_NAME>\n EXIT" << std::endl;

    // Main input loop
    std::string input;
    while (running) {
        if (!std::getline(std::cin, input)) break;
        if (input == "EXIT") {
            running = false;
        } else {
            std::stringstream ss(input);
            std::string cmd;
            ss >> cmd;

            if (cmd == "LIST") {
                PacketHeader header;
                header.command = CMD_LIST_USERS;
                header.size = 0;
                send(socketFd, &header, sizeof(header), 0);
            } else if (cmd == "LEADERBOARD") {
                 PacketHeader header;
                header.command = CMD_LEADERBOARD;
                header.size = 0;
                send(socketFd, &header, sizeof(header), 0);               
            } else if (cmd == "REGISTER" || cmd == "LOGIN") {
                std::string user, pass;
                ss >> user >> pass;
                if (user.empty() || pass.empty()) {
                    std::cout << "Usage: " << cmd << " <username> <password>" << std::endl;
                    continue;
                }
                
                LoginRequest req;
                strncpy(req.username, user.c_str(), 32);
                strncpy(req.password, pass.c_str(), 32);

                PacketHeader header;
                header.command = (cmd == "REGISTER") ? CMD_REGISTER : CMD_LOGIN;
                header.size = sizeof(LoginRequest);

                // Send Header
                send(socketFd, &header, sizeof(header), 0);
                // Send Body
                send(socketFd, &req, sizeof(req), 0);
            } else if (cmd == "CHALLENGE") {
                std::string target;
                ss >> target;
                if (target.empty()) {
                    std::cout << "Usage: CHALLENGE <username>" << std::endl;
                    continue;
                }
                ChallengePacket pkt;
                strncpy(pkt.targetUser, target.c_str(), 32);
                
                PacketHeader header;
                header.command = CMD_CHALLENGE_REQ;
                header.size = sizeof(ChallengePacket);
                
                send(socketFd, &header, sizeof(header), 0);
                send(socketFd, &pkt, sizeof(pkt), 0);
                std::cout << "Challenge sent to " << target << std::endl;

            } else if (cmd == "ACCEPT") {
                std::string target; // Who challenged us
                ss >> target;
                if (target.empty()) {
                    std::cout << "Usage: ACCEPT <challenger_name>" << std::endl;
                    continue;
                }
                ChallengePacket pkt;
                strncpy(pkt.targetUser, target.c_str(), 32); // Send back who we are accepting
                
                PacketHeader header;
                header.command = CMD_CHALLENGE_RESP; // Accepting
                header.size = sizeof(ChallengePacket);
                
                send(socketFd, &header, sizeof(header), 0);
                send(socketFd, &pkt, sizeof(pkt), 0);
                std::cout << "Accepted challenge from " << target << std::endl;

            } else if (cmd == "SHOOT") {
                std::string target;
                ss >> target;
                MovePayload payload;
                payload.itemType = 0;
                if (target == "SELF") {
                    payload.moveType = SHOOT_SELF;
                } else if (target == "OPPONENT") {
                    payload.moveType = SHOOT_OPPONENT;
                } else {
                     std::cout << "Usage: SHOOT <SELF|OPPONENT>" << std::endl;
                     continue;
                }
                
                PacketHeader header;
                header.command = CMD_GAME_MOVE;
                header.size = sizeof(MovePayload);
                send(socketFd, &header, sizeof(header), 0);
                send(socketFd, &payload, sizeof(payload), 0);
            
            } else if (cmd == "USE") {
                std::string itemStr;
                ss >> itemStr;
                ItemType item = ITEM_NONE;
                if (itemStr == "BEER") item = ITEM_BEER;
                else if (itemStr == "CIGS") item = ITEM_CIGARETTES;
                else if (itemStr == "CUFFS") item = ITEM_HANDCUFFS;
                else if (itemStr == "GLASS") item = ITEM_MAGNIFYING_GLASS;
                else if (itemStr == "KNIFE") item = ITEM_KNIFE;
                else if (itemStr == "INVERT") item = ITEM_INVERTER;
                else if (itemStr == "MEDS") item = ITEM_EXPIRED_MEDICINE;
                else {
                    std::cout << "Unknown item. Use BEER, CIGS, CUFFS, GLASS, KNIFE, INVERT, MEDS." << std::endl;
                    continue;
                }
                
                MovePayload payload;
                payload.moveType = USE_ITEM;
                payload.itemType = item;
                
                PacketHeader header;
                header.command = CMD_GAME_MOVE;
                header.size = sizeof(MovePayload);
                send(socketFd, &header, sizeof(header), 0);
                send(socketFd, &payload, sizeof(payload), 0);
                
            } else {
                 std::cout << "Unknown command. Use REGISTER, LOGIN, LIST, LEADERBOARD, CHALLENGE, ACCEPT, SHOOT, USE or EXIT." << std::endl;
            }
        }
    }
    
    close(socketFd);
}

}
