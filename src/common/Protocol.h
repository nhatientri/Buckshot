#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Buckshot {

// Payload/packet helper
struct PacketHeader {
    uint32_t size; // Body size
    uint8_t command;
};

// Command Codes
enum Command : uint8_t {
    CMD_OK = 0, // Used for success responses
    CMD_REGISTER = 1,
    CMD_LOGIN = 2,
    CMD_FAIL = 255, // Explicit fail code
    // Duplicates removed
    
    CMD_LIST_USERS = 5,
    CMD_LIST_USERS_RESP = 6,

    CMD_LEADERBOARD = 30,
    CMD_LEADERBOARD_RESP = 31,

    CMD_CHALLENGE_REQ = 10,
    CMD_CHALLENGE_RESP = 11, // Accept/Decline

    CMD_GAME_START = 20,
    CMD_GAME_MOVE = 21,
    CMD_GAME_STATE = 22, // Update board
    CMD_GAME_RESULT = 23, // Win/Loss/Draw
    
    // Replay
    CMD_LIST_REPLAYS   = 40,
    CMD_LIST_REPLAYS_RESP = 41,
    CMD_GET_REPLAY     = 42,
    CMD_REPLAY_DATA    = 43,

    // AI
    CMD_PLAY_AI = 45,

    // Resign
    // Resign
    CMD_RESIGN         = 50,

    // Matchmaking
    CMD_QUEUE_JOIN     = 60,
    CMD_QUEUE_LEAVE    = 61,

    // Pause
    CMD_TOGGLE_PAUSE   = 70,

    // History
    CMD_GET_HISTORY    = 80,
    CMD_HISTORY_DATA   = 81,

    CMD_ERROR = 99
};

struct HistoryEntry {
    char timestamp[32];
    char opponent[32];
    char result[8]; // "WIN", "LOSS", "DRAW"?
    int32_t eloChange;
    char replayFile[64]; // Filename to request replay
};

// Fixed size structs for simplicity in "raw socket" context, 
// or serialization helpers for strings.

struct LoginRequest {
    char username[32];
    char password[32];
};

struct ChallengePacket {
    char targetUser[32]; // Who you challenge, or who challenged you
};

enum MoveType : uint8_t {
    SHOOT_SELF = 1,
    SHOOT_OPPONENT = 2,
    USE_ITEM = 3
};

enum ItemType : uint8_t {
    ITEM_NONE = 0,
    ITEM_BEER = 1,
    ITEM_CIGARETTES = 2,
    ITEM_HANDCUFFS = 3,
    ITEM_MAGNIFYING_GLASS = 4,
    ITEM_KNIFE = 5,
    ITEM_INVERTER = 6,
    ITEM_EXPIRED_MEDICINE = 7
};

struct MovePayload {
    uint8_t moveType; // SHOOT_SELF, SHOOT_OPPONENT, USE_ITEM
    uint8_t itemType; // Only if USE_ITEM
};

struct GameStatePacket {
    int p1Hp;
    int p2Hp;
    int shellsRemaining;
    int liveCount; // Start of round count
    int blankCount; // Start of round count
    
    uint8_t p1Inventory[8];
    uint8_t p2Inventory[8];
    bool p1Handcuffed;
    bool p2Handcuffed;
    bool knifeActive;
    
    char currentTurnUser[32]; // Username of whose turn it is
    char p1Name[32];
    char p2Name[32];
    char message[128]; // "Player1 shot Self! It was a Blank."
    bool gameOver;
    char winner[32];
    int32_t turnTimeRemaining; // Seconds remaining for current turn
    int32_t p1EloChange; // Delta for P1
    int32_t p2EloChange; // Delta for P2
    bool isPaused;
};

// Response codes
enum ResponseCode : uint8_t {
    RES_OK = 0,
    RES_FAIL = 1,
    RES_DECLINED = 2
};

} // namespace Buckshot
