#ifndef GAMESTATE_H
#define GAMESTATE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ITEMS 6
#define MAX_USERNAME_LEN 30

// Định nghĩa trạng thái người chơi
typedef struct {
    int lives;
    char items[MAX_ITEMS][30]; // Ví dụ: 6 items, mỗi item 30 ký tự
    int item_count;
} PlayerState;

// Cấu trúc để lưu một mục trong lịch sử game (cho nhiệm vụ View History)
typedef struct {
    int game_id;
    char opponent_name[MAX_USERNAME_LEN];
    char result[5]; // "WIN" hoặc "LOSS"
    int elo_new;
    int elo_change;
    char timestamp[30]; 
} GameHistoryEntry;

// Cấu trúc trạng thái game hiện tại
typedef struct {
    int client_socket;
    int current_elo;
    char session_token[50];
    char username[MAX_USERNAME_LEN];
    
    int game_id;
    char turn[10]; // "YOURS" hoặc "OPPONENT"
    int live_shells;
    int blank_shells;
    
    PlayerState self;
    PlayerState opponent;
    char opponent_name[MAX_USERNAME_LEN];
    
    // Lưu trữ kết quả game gần nhất để hiển thị
    int last_elo_change;
} CurrentGameState;

// Khai báo instance toàn cục
extern CurrentGameState g_GameState;

#endif // GAMESTATE_H