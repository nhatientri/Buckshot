#include "protocol.h"
#include "history_manager.h"
#include "gui.h"       // <--- THÊM FILE GUI
#include "gamestate.h" // Cần để truy cập MAX_ITEMS
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// Bổ sung include cho GTK/GLib (để sử dụng g_idle_add)
#include <glib.h> 

CurrentGameState g_GameState;
int g_client_socket = -1;

// =========================================================
// GTK+ THREAD SAFETY WRAPPERS (Chuyển công việc sang luồng chính)
// =========================================================

// Wrapper cho cập nhật trạng thái game (Lives, Shells, Turn)
static gboolean update_game_state_wrapper(gpointer data) {
    (void)data; // Không sử dụng
    // Gọi hàm cập nhật GUI chính
    gui_update_lives(g_GameState.self.lives, g_GameState.opponent.lives);
    gui_update_shells(g_GameState.live_shells, g_GameState.blank_shells);
    // [Cần gọi hàm gui_update_items() nếu bạn triển khai]
    
    return G_SOURCE_REMOVE; // Chỉ chạy một lần
}

// Wrapper cho hiển thị cửa sổ Game Over
static gboolean show_game_over_wrapper(gpointer data) {
    // Sử dụng giá trị ELO change đã được lưu trong g_GameState
    gui_show_game_over(
        (char*)data, // Dữ liệu truyền vào (WIN/LOSS)
        g_GameState.last_elo_change // Sử dụng giá trị ELO change thực tế từ server
    );
    
    return G_SOURCE_REMOVE;
}

// Wrapper cho hiển thị rematch request
static gboolean show_rematch_request_wrapper(gpointer data) {
    gui_show_rematch_request((const char*)data);
    return G_SOURCE_REMOVE;
}

// Wrapper cho ẩn nút rematch
static gboolean hide_rematch_buttons_wrapper(gpointer data) {
    (void)data;
    gui_hide_rematch_buttons();
    return G_SOURCE_REMOVE;
}

// Wrapper cho cập nhật status label
typedef struct {
    GtkWidget *label;
    const char *text;
} StatusUpdateData;

static gboolean update_status_wrapper(gpointer data) {
    StatusUpdateData *update = (StatusUpdateData*)data;
    if (update && update->label && update->text) {
        gtk_label_set_text(GTK_LABEL(update->label), update->text);
    }
    if (update) {
        free(update);
    }
    return G_SOURCE_REMOVE;
}

// Wrapper cho bật nút rematch
static gboolean enable_rematch_button_wrapper(gpointer data) {
    (void)data;
    extern GtkWidget *g_btn_rematch;
    if (g_btn_rematch) {
        gtk_widget_set_sensitive(g_btn_rematch, TRUE);
    }
    return G_SOURCE_REMOVE;
}

// Wrapper cho bật nút resign
static gboolean enable_resign_button_wrapper(gpointer data) {
    (void)data;
    gui_enable_resign_button();
    return G_SOURCE_REMOVE;
}

// Hàm gửi tin nhắn (COMMAND {"data":"value"})
void send_message(const char* command, struct json_object* payload) {
    if (g_client_socket < 0) return;

    // Chuyển JSON object thành chuỗi
    const char *json_string = json_object_to_json_string(payload);
    
    // Format: COMMAND {"data":"value"}\n
    char message[4096];
    snprintf(message, sizeof(message), "%s %s\n", command, json_string);
    
    if (send(g_client_socket, message, strlen(message), 0) < 0) {
        perror("Error sending message");
    }
    
    // Giải phóng JSON object sau khi dùng
    json_object_put(payload);
}

void handle_game_over(struct json_object* payload) {
    struct json_object *jresult, *jnew_elo, *jchange, *jgame_id;
    const char *result = NULL;
    int new_elo = 0, elo_change = 0, game_id = 0;

    if (json_object_object_get_ex(payload, "result", &jresult))
        result = json_object_get_string(jresult);
    if (json_object_object_get_ex(payload, "elo_new", &jnew_elo))
        new_elo = json_object_get_int(jnew_elo);
    if (json_object_object_get_ex(payload, "elo_change", &jchange))
        elo_change = json_object_get_int(jchange);
    if (json_object_object_get_ex(payload, "game_id", &jgame_id))
        game_id = json_object_get_int(jgame_id);

    g_GameState.current_elo = new_elo;
    g_GameState.last_elo_change = elo_change; // Lưu ELO change để hiển thị

    // Nhiệm vụ: Log game data (lưu vào file CSV)
    log_game_entry(game_id, result, new_elo, elo_change, g_GameState.opponent_name);
    
    // Nhiệm vụ: Transmit game results and logs (gửi lên server)
    send_game_logs(game_id, result, new_elo, elo_change, g_GameState.opponent_name);

    if (result) {
        g_idle_add(show_game_over_wrapper, (gpointer)result);
    }
}

void handle_login_success(struct json_object* payload) {
    struct json_object *jtoken, *jusername, *jelo;

    if (json_object_object_get_ex(payload, "token", &jtoken)) {
        strncpy(g_GameState.session_token, json_object_get_string(jtoken), sizeof(g_GameState.session_token) - 1);
    }
    if (json_object_object_get_ex(payload, "username", &jusername)) {
        strncpy(g_GameState.username, json_object_get_string(jusername), sizeof(g_GameState.username) - 1);
    }
    if (json_object_object_get_ex(payload, "elo", &jelo)) {
        g_GameState.current_elo = json_object_get_int(jelo);
    }

    g_print("Login Success! Token: %s, ELO: %d\n", g_GameState.session_token, g_GameState.current_elo);
}

void handle_match_found(struct json_object* payload) {
    struct json_object *jopponent, *jgame_id;
    
    if (json_object_object_get_ex(payload, "opponent", &jopponent)) {
        strncpy(g_GameState.opponent_name, 
                json_object_get_string(jopponent), 
                sizeof(g_GameState.opponent_name) - 1);
    }
    if (json_object_object_get_ex(payload, "game_id", &jgame_id)) {
        g_GameState.game_id = json_object_get_int(jgame_id);
    }
    
    g_print("Match Found! Opponent: %s (Game ID: %d)\n", g_GameState.opponent_name, g_GameState.game_id);
    
    // Bật nút Resign sau khi match (sử dụng wrapper an toàn)
    g_idle_add(enable_resign_button_wrapper, NULL);
}

// Handler cho REMATCH_REQUEST từ đối thủ
void handle_rematch_request(struct json_object* payload) {
    struct json_object *jopponent;
    const char *opponent_name = NULL;
    
    if (json_object_object_get_ex(payload, "opponent", &jopponent)) {
        opponent_name = json_object_get_string(jopponent);
    }
    
    g_print("Received REMATCH_REQUEST from %s\n", opponent_name ? opponent_name : "opponent");
    
    // Hiển thị nút Accept/Reject trong GUI (sử dụng wrapper an toàn)
    g_idle_add(show_rematch_request_wrapper, (gpointer)opponent_name);
}

// Handler cho REMATCH_ACCEPTED từ server
void handle_rematch_accepted(struct json_object* payload) {
    struct json_object *jgame_id;
    int game_id = 0;
    
    if (json_object_object_get_ex(payload, "game_id", &jgame_id)) {
        game_id = json_object_get_int(jgame_id);
        g_GameState.game_id = game_id;
    }
    
    g_print("Rematch ACCEPTED! New Game ID: %d\n", game_id);
    
    // Ẩn nút rematch và cập nhật GUI
    g_idle_add(hide_rematch_buttons_wrapper, NULL);
    
    // Cập nhật status label
    if (g_lbl_status) {
        StatusUpdateData *update = (StatusUpdateData*)malloc(sizeof(StatusUpdateData));
        update->label = g_lbl_status;
        update->text = "Rematch accepted! Starting new game...";
        g_idle_add(update_status_wrapper, update);
    }
}

// Handler cho REMATCH_REJECTED từ server
void handle_rematch_rejected(struct json_object* payload) {
    (void)payload;
    g_print("Rematch REJECTED by opponent\n");
    
    // Ẩn nút và cập nhật GUI
    g_idle_add(hide_rematch_buttons_wrapper, NULL);
    
    // Cập nhật status label
    if (g_lbl_status) {
        StatusUpdateData *update = (StatusUpdateData*)malloc(sizeof(StatusUpdateData));
        update->label = g_lbl_status;
        update->text = "Rematch rejected by opponent.";
        g_idle_add(update_status_wrapper, update);
    }
    
    // Bật lại nút Request Rematch
    g_idle_add(enable_rematch_button_wrapper, NULL);
}

// Thêm hàm handle_game_state vào src/protocol.c
void handle_game_state(struct json_object* payload) {
    struct json_object *jturn, *jlives, *jshells, *jitems;
    
    // 1. Cập nhật ID và Lượt chơi
    if (json_object_object_get_ex(payload, "turn", &jturn))
        strncpy(g_GameState.turn, json_object_get_string(jturn), sizeof(g_GameState.turn) - 1);
    
    // 2. Cập nhật Lives
    if (json_object_object_get_ex(payload, "lives", &jlives)) {
        struct json_object *jyours, *jopponent;
        if (json_object_object_get_ex(jlives, "yours", &jyours))
            g_GameState.self.lives = json_object_get_int(jyours);
        if (json_object_object_get_ex(jlives, "opponent", &jopponent))
            g_GameState.opponent.lives = json_object_get_int(jopponent);
    }
    
    // 3. Cập nhật Shells
    if (json_object_object_get_ex(payload, "shells", &jshells)) {
        struct json_object *jlive, *jblank;
        if (json_object_object_get_ex(jshells, "live", &jlive))
            g_GameState.live_shells = json_object_get_int(jlive);
        if (json_object_object_get_ex(jshells, "blank", &jblank))
            g_GameState.blank_shells = json_object_get_int(jblank);
    }

    // 4. Cập nhật Items (Đảm bảo MAX_ITEMS được định nghĩa trong gamestate.h)
    if (json_object_object_get_ex(payload, "items", &jitems) && json_object_is_type(jitems, json_type_array)) {
        int i;
        g_GameState.self.item_count = 0; 
        int array_len = json_object_array_length(jitems);
        
        for (i = 0; i < array_len && i < MAX_ITEMS; i++) {
            struct json_object *jitem = json_object_array_get_idx(jitems, i);
            if (jitem) {
                strncpy(g_GameState.self.items[i], json_object_get_string(jitem), 
                        sizeof(g_GameState.self.items[i]) - 1);
                g_GameState.self.item_count++;
            }
        }
    }
    
    g_print("Game State: Turn=%s, Lives: You=%d, Opponent=%d, Shells: Live=%d, Blank=%d\n", 
           g_GameState.turn, g_GameState.self.lives, g_GameState.opponent.lives,
           g_GameState.live_shells, g_GameState.blank_shells);
    
    // GỌI HÀM CẬP NHẬT GUI AN TOÀN
    g_idle_add(update_game_state_wrapper, NULL);
}

void handle_server_message(const char* raw_message) {
    char command[50];
    const char *json_body_start = strchr(raw_message, '{');

    if (!json_body_start) {
        return;
    }

    int command_len = json_body_start - raw_message;
    if (command_len >= (int)sizeof(command))
        command_len = sizeof(command) - 1;
    strncpy(command, raw_message, command_len);
    command[command_len] = '\0';

    struct json_object *payload = json_tokener_parse(json_body_start);
    if (!payload) {
        fprintf(stderr, "JSON Parse Error for message: %s\n", raw_message);
        return;
    }

    while (strlen(command) > 0 && command[strlen(command) - 1] == ' ') {
        command[strlen(command) - 1] = '\0';
    }

    if (strcmp(command, "LOGIN_SUCCESS") == 0) {
        handle_login_success(payload);
    } else if (strcmp(command, "GAME_OVER") == 0) {
        handle_game_over(payload);
    } else if (strcmp(command, "GAME_STATE") == 0) {
        handle_game_state(payload);
    } else if (strcmp(command, "MATCH_FOUND") == 0) {
        handle_match_found(payload);
    } else if (strcmp(command, "REMATCH_REQUEST") == 0) {
        handle_rematch_request(payload);
    } else if (strcmp(command, "REMATCH_ACCEPTED") == 0) {
        handle_rematch_accepted(payload);
    } else if (strcmp(command, "REMATCH_REJECTED") == 0) {
        handle_rematch_rejected(payload);
    }

    json_object_put(payload);
}

// --- Các hàm hành động của Client ---

// Nhiệm vụ: Offer resignation
void send_resign_game() {
    if (g_GameState.game_id == -1 || g_GameState.session_token[0] == '\0') return;
    
    // GAME_RESIGN {"token": "123abc", "game_id": 77} 
    struct json_object *payload = json_object_new_object();
    json_object_object_add(payload, "token", json_object_new_string(g_GameState.session_token));
    json_object_object_add(payload, "game_id", json_object_new_int(g_GameState.game_id));
    
    send_message("GAME_RESIGN", payload);
    g_print("Sent GAME_RESIGN\n");

    // [GUI Logic]: Xử lý vô hiệu hóa nút được thực hiện trong callback on_resign_button_clicked
}

// Nhiệm vụ: Request rematch
void send_rematch_request() {
    // Đây là lệnh custom, cần thống nhất với đồng đội!
    // Giả định lệnh là REMATCH_REQUEST
    struct json_object *payload = json_object_new_object();
    json_object_object_add(payload, "token", json_object_new_string(g_GameState.session_token)); 
    json_object_object_add(payload, "opponent", json_object_new_string(g_GameState.opponent_name)); 
    
    send_message("REMATCH_REQUEST", payload);
    g_print("Sent REMATCH_REQUEST\n");

    // [GUI Logic]: Xử lý hiển thị trạng thái được thực hiện trong callback on_rematch_button_clicked
}

// Accept rematch request
void send_rematch_accept() {
    if (g_GameState.session_token[0] == '\0') return;
    
    struct json_object *payload = json_object_new_object();
    json_object_object_add(payload, "token", json_object_new_string(g_GameState.session_token));
    json_object_object_add(payload, "opponent", json_object_new_string(g_GameState.opponent_name));
    
    send_message("REMATCH_ACCEPT", payload);
    g_print("Sent REMATCH_ACCEPT\n");
}

// Reject rematch request
void send_rematch_reject() {
    if (g_GameState.session_token[0] == '\0') return;
    
    struct json_object *payload = json_object_new_object();
    json_object_object_add(payload, "token", json_object_new_string(g_GameState.session_token));
    json_object_object_add(payload, "opponent", json_object_new_string(g_GameState.opponent_name));
    
    send_message("REMATCH_REJECT", payload);
    g_print("Sent REMATCH_REJECT\n");
}

// Nhiệm vụ: Transmit game results and logs
void send_game_logs(int game_id, const char* result, int elo_new, int elo_change, const char* opponent) {
    if (g_GameState.session_token[0] == '\0') return;
    
    // Gửi log game lên server với format: GAME_LOG {"token": "...", "game_id": ..., "result": "...", "elo_new": ..., "elo_change": ..., "opponent": "..."}
    struct json_object *payload = json_object_new_object();
    json_object_object_add(payload, "token", json_object_new_string(g_GameState.session_token));
    json_object_object_add(payload, "game_id", json_object_new_int(game_id));
    json_object_object_add(payload, "result", json_object_new_string(result));
    json_object_object_add(payload, "elo_new", json_object_new_int(elo_new));
    json_object_object_add(payload, "elo_change", json_object_new_int(elo_change));
    json_object_object_add(payload, "opponent", json_object_new_string(opponent));
    
    send_message("GAME_LOG", payload);
    g_print("Sent GAME_LOG: game_id=%d, result=%s, elo_change=%d\n", game_id, result, elo_change);
}

// File: src/protocol.c

