#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <json-c/json.h>
#include "gamestate.h"

// Khai báo socket ID để các hàm có thể sử dụng
extern int g_client_socket;

void send_message(const char* command, struct json_object* payload);
void* receive_data_thread(void* arg);
void handle_server_message(const char* raw_message);
void handle_match_found(struct json_object* payload);
void handle_game_state(struct json_object* payload);
void handle_rematch_request(struct json_object* payload);
void handle_rematch_accepted(struct json_object* payload);
void handle_rematch_rejected(struct json_object* payload);
// Các hàm hành động của Client
void send_login(const char* username, const char* pass_hash);
void send_resign_game(); // Nhiệm vụ: Offer resignation
void send_rematch_request(); // Nhiệm vụ: Request rematch
void send_rematch_accept(); // Accept rematch request
void send_rematch_reject(); // Reject rematch request
void send_game_logs(int game_id, const char* result, int elo_new, int elo_change, const char* opponent); // Nhiệm vụ: Transmit game results and logs

#endif // PROTOCOL_H