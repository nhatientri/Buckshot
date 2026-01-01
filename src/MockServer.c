#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <json-c/json.h>

#define SERVER_PORT 8080
#define MAX_CLIENTS 2

// Cấu trúc lưu thông tin client
typedef struct {
    int socket;
    char username[50];
    char token[50];
    int elo;
    int game_id;
    int opponent_socket; // Socket của đối thủ
} ClientInfo;

// Mảng lưu thông tin các client
ClientInfo clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// Hàm tìm client theo socket
ClientInfo* find_client_by_socket(int sock) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i].socket == sock) {
            return &clients[i];
        }
    }
    return NULL;
}

// Hàm tìm client theo token
ClientInfo* find_client_by_token(const char* token) {
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].token, token) == 0) {
            return &clients[i];
        }
    }
    return NULL;
}

// Forward declaration
void match_clients(void);
void send_game_over_auto(ClientInfo* client1, ClientInfo* client2);

// Cấu trúc để truyền dữ liệu vào thread
typedef struct {
    ClientInfo* client1;
    ClientInfo* client2;
} GameOverData;

// Thread function để tự động gửi GAME_OVER
void* auto_game_over_thread_func(void* data) {
    GameOverData* game_data = (GameOverData*)data;
    sleep(5); // Chờ 5 giây
    printf("MockServer: Auto-sending GAME_OVER after 5 seconds...\n");
    send_game_over_auto(game_data->client1, game_data->client2);
    free(game_data);
    return NULL;
}

// Hàm gửi message đến client
void send_to_client(int sock, const char* message) {
    if (send(sock, message, strlen(message), 0) < 0) {
        perror("Send failed");
    }
}

// Hàm xử lý LOGIN
void handle_login(int sock, struct json_object* payload) {
    struct json_object *jusername;
    const char *username = "player";
    
    if (json_object_object_get_ex(payload, "username", &jusername)) {
        username = json_object_get_string(jusername);
    }
    
    pthread_mutex_lock(&clients_mutex);
    
    // Tìm hoặc tạo client mới
    ClientInfo* client = find_client_by_socket(sock);
    if (!client && client_count < MAX_CLIENTS) {
        client = &clients[client_count];
        client->socket = sock;
        strncpy(client->username, username, sizeof(client->username) - 1);
        snprintf(client->token, sizeof(client->token), "token_%d", client_count + 1);
        client->elo = 1250;
        client->game_id = 0;
        client->opponent_socket = -1;
        client_count++;
        printf("MockServer: Client %d logged in as %s (token: %s)\n", 
               client_count, client->username, client->token);
    }
    
    pthread_mutex_unlock(&clients_mutex);
    
    if (client) {
        // Gửi LOGIN_SUCCESS
        char login_response[512];
        snprintf(login_response, sizeof(login_response),
                "LOGIN_SUCCESS {\"token\": \"%s\", \"username\": \"%s\", \"elo\": %d}\n",
                client->token, client->username, client->elo);
        send_to_client(sock, login_response);
        printf("MockServer: Sent LOGIN_SUCCESS to %s\n", client->username);
        
        // Nếu có 2 client, tự động match
        if (client_count == 2) {
            sleep(1);
            match_clients();
        }
    }
}

// Hàm gửi GAME_OVER tự động (để test)
void send_game_over_auto(ClientInfo* client1, ClientInfo* client2) {
    if (!client1 || !client2) return;
    
    int game_id = client1->game_id;
    char win_msg[512], loss_msg[512];
    
    // Client 1 thắng, Client 2 thua
    int new_elo1 = client1->elo + 15;
    int new_elo2 = client2->elo - 10;
    
    snprintf(win_msg, sizeof(win_msg),
            "GAME_OVER {\"game_id\": %d, \"result\": \"WIN\", \"elo_new\": %d, \"elo_change\": 15}\n",
            game_id, new_elo1);
    snprintf(loss_msg, sizeof(loss_msg),
            "GAME_OVER {\"game_id\": %d, \"result\": \"LOSS\", \"elo_new\": %d, \"elo_change\": -10}\n",
            game_id, new_elo2);
    
    send_to_client(client1->socket, win_msg);
    send_to_client(client2->socket, loss_msg);
    
    // Cập nhật ELO
    client1->elo = new_elo1;
    client2->elo = new_elo2;
    
    printf("MockServer: Sent GAME_OVER (Auto) - %s WINS, %s LOSES (Game ID: %d)\n",
           client1->username, client2->username, game_id);
}

// Hàm match 2 client với nhau
void match_clients(void) {
    if (client_count < 2) return;
    
    ClientInfo* client1 = &clients[0];
    ClientInfo* client2 = &clients[1];
    
    int game_id = 100 + client_count;
    client1->game_id = game_id;
    client2->game_id = game_id;
    client1->opponent_socket = client2->socket;
    client2->opponent_socket = client1->socket;
    
    // Gửi MATCH_FOUND cho cả 2 client
    char match_msg1[512], match_msg2[512];
    snprintf(match_msg1, sizeof(match_msg1),
            "MATCH_FOUND {\"game_id\": %d, \"opponent\": \"%s\", \"elo\": %d}\n",
            game_id, client2->username, client2->elo);
    snprintf(match_msg2, sizeof(match_msg2),
            "MATCH_FOUND {\"game_id\": %d, \"opponent\": \"%s\", \"elo\": %d}\n",
            game_id, client1->username, client1->elo);
    
    send_to_client(client1->socket, match_msg1);
    send_to_client(client2->socket, match_msg2);
    printf("MockServer: Matched %s and %s (Game ID: %d)\n", 
           client1->username, client2->username, game_id);
    
    // Tạo thread để tự động gửi GAME_OVER sau 5 giây (để test rematch)
    // (Trong thực tế, game sẽ kết thúc khi một trong hai player thua hết lives)
    pthread_t auto_game_over_thread;
    GameOverData* game_data = (GameOverData*)malloc(sizeof(GameOverData));
    game_data->client1 = client1;
    game_data->client2 = client2;
    
    if (pthread_create(&auto_game_over_thread, NULL, auto_game_over_thread_func, game_data) == 0) {
        pthread_detach(auto_game_over_thread);
    } else {
        free(game_data);
    }
}

// Hàm xử lý REMATCH_REQUEST
void handle_rematch_request(int sock, struct json_object* payload) {
    ClientInfo* sender = find_client_by_socket(sock);
    if (!sender || sender->opponent_socket < 0) {
        printf("MockServer: Invalid rematch request\n");
        return;
    }
    
    ClientInfo* opponent = find_client_by_socket(sender->opponent_socket);
    if (!opponent) {
        printf("MockServer: Opponent not found\n");
        return;
    }
    
    // Chuyển REMATCH_REQUEST đến đối thủ
    char rematch_msg[512];
    snprintf(rematch_msg, sizeof(rematch_msg),
            "REMATCH_REQUEST {\"opponent\": \"%s\"}\n",
            sender->username);
    send_to_client(opponent->socket, rematch_msg);
    printf("MockServer: Forwarded REMATCH_REQUEST from %s to %s\n",
           sender->username, opponent->username);
}

// Hàm xử lý REMATCH_ACCEPT
void handle_rematch_accept(int sock, struct json_object* payload) {
    ClientInfo* accepter = find_client_by_socket(sock);
    if (!accepter || accepter->opponent_socket < 0) {
        printf("MockServer: Invalid rematch accept\n");
        return;
    }
    
    ClientInfo* opponent = find_client_by_socket(accepter->opponent_socket);
    if (!opponent) {
        printf("MockServer: Opponent not found\n");
        return;
    }
    
    // Tạo game mới
    int new_game_id = accepter->game_id + 1;
    accepter->game_id = new_game_id;
    opponent->game_id = new_game_id;
    
    // Gửi REMATCH_ACCEPTED cho cả 2 client
    char accept_msg1[512], accept_msg2[512];
    snprintf(accept_msg1, sizeof(accept_msg1),
            "REMATCH_ACCEPTED {\"game_id\": %d}\n", new_game_id);
    snprintf(accept_msg2, sizeof(accept_msg2),
            "REMATCH_ACCEPTED {\"game_id\": %d}\n", new_game_id);
    
    send_to_client(accepter->socket, accept_msg1);
    send_to_client(opponent->socket, accept_msg2);
    printf("MockServer: Rematch accepted! New Game ID: %d\n", new_game_id);
    
    // Gửi MATCH_FOUND lại để bắt đầu game mới
    sleep(1);
    match_clients();
}

// Hàm xử lý REMATCH_REJECT
void handle_rematch_reject(int sock, struct json_object* payload) {
    ClientInfo* rejecter = find_client_by_socket(sock);
    if (!rejecter || rejecter->opponent_socket < 0) {
        printf("MockServer: Invalid rematch reject\n");
        return;
    }
    
    ClientInfo* opponent = find_client_by_socket(rejecter->opponent_socket);
    if (!opponent) {
        printf("MockServer: Opponent not found\n");
        return;
    }
    
    // Gửi REMATCH_REJECTED cho đối thủ
    char reject_msg[512];
    snprintf(reject_msg, sizeof(reject_msg),
            "REMATCH_REJECTED {}\n");
    send_to_client(opponent->socket, reject_msg);
    printf("MockServer: Rematch rejected by %s\n", rejecter->username);
}

// Hàm xử lý GAME_OVER
void handle_game_over(int sock, struct json_object* payload) {
    ClientInfo* client = find_client_by_socket(sock);
    if (!client) return;
    
    // Gửi GAME_OVER cho cả 2 client (ví dụ: client này thắng, đối thủ thua)
    int game_id = client->game_id;
    char win_msg[512], loss_msg[512];
    
    snprintf(win_msg, sizeof(win_msg),
            "GAME_OVER {\"game_id\": %d, \"result\": \"WIN\", \"elo_new\": %d, \"elo_change\": 15}\n",
            game_id, client->elo + 15);
    snprintf(loss_msg, sizeof(loss_msg),
            "GAME_OVER {\"game_id\": %d, \"result\": \"LOSS\", \"elo_new\": %d, \"elo_change\": -10}\n",
            game_id, client->elo - 10);
    
    send_to_client(client->socket, win_msg);
    if (client->opponent_socket >= 0) {
        send_to_client(client->opponent_socket, loss_msg);
    }
    printf("MockServer: Sent GAME_OVER messages\n");
}

// Hàm parse và xử lý message từ client
void handle_client_message(int sock, const char* raw_message) {
    char command[50];
    const char *json_body_start = strchr(raw_message, '{');
    
    if (!json_body_start) {
        printf("MockServer: Invalid message format\n");
        return;
    }
    
    int command_len = json_body_start - raw_message;
    if (command_len >= (int)sizeof(command))
        command_len = sizeof(command) - 1;
    strncpy(command, raw_message, command_len);
    command[command_len] = '\0';
    
    // Loại bỏ khoảng trắng ở cuối
    while (strlen(command) > 0 && command[strlen(command) - 1] == ' ') {
        command[strlen(command) - 1] = '\0';
    }
    
    struct json_object *payload = json_tokener_parse(json_body_start);
    if (!payload) {
        printf("MockServer: JSON parse error\n");
        return;
    }
    
    printf("MockServer: Received command: %s\n", command);
    
    if (strcmp(command, "LOGIN") == 0) {
        handle_login(sock, payload);
    } else if (strcmp(command, "REMATCH_REQUEST") == 0) {
        handle_rematch_request(sock, payload);
    } else if (strcmp(command, "REMATCH_ACCEPT") == 0) {
        handle_rematch_accept(sock, payload);
    } else if (strcmp(command, "REMATCH_REJECT") == 0) {
        handle_rematch_reject(sock, payload);
    } else if (strcmp(command, "GAME_OVER") == 0) {
        handle_game_over(sock, payload);
    } else if (strcmp(command, "GAME_RESIGN") == 0) {
        // Khi một client resign, gửi GAME_OVER cho cả 2
        ClientInfo* resigner = find_client_by_socket(sock);
        if (resigner && resigner->opponent_socket >= 0) {
            ClientInfo* opponent = find_client_by_socket(resigner->opponent_socket);
            if (opponent) {
                // Đối thủ thắng, người resign thua
                send_game_over_auto(opponent, resigner);
            }
        }
    } else {
        printf("MockServer: Unknown command: %s\n", command);
    }
    
    json_object_put(payload);
}

// Thread handler cho mỗi client
void* connection_handler(void* socket_desc) {
    int sock = *(int*)socket_desc;
    char client_message[4096];
    int read_size;
    
    printf("MockServer: Client connected (socket: %d)\n", sock);
    
    // Đọc và xử lý messages từ client
    while ((read_size = recv(sock, client_message, sizeof(client_message) - 1, 0)) > 0) {
        client_message[read_size] = '\0';
        
        // Xử lý nhiều messages nếu có (phân tách bằng \n)
        char *line = client_message;
        char *next_line;
        while ((next_line = strchr(line, '\n')) != NULL) {
            *next_line = '\0';
            if (strlen(line) > 0) {
                handle_client_message(sock, line);
            }
            line = next_line + 1;
        }
        // Xử lý dòng cuối cùng nếu không có \n
        if (strlen(line) > 0) {
            handle_client_message(sock, line);
        }
    }
    
    if (read_size == 0) {
        printf("MockServer: Client disconnected (socket: %d)\n", sock);
    } else if (read_size == -1) {
        perror("recv failed");
    }
    
    // Xóa client khỏi danh sách
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (clients[i].socket == sock) {
            // Di chuyển các client còn lại lên
            for (int j = i; j < client_count - 1; j++) {
                clients[j] = clients[j + 1];
            }
            client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    
    close(sock);
    free(socket_desc);
    return NULL;
}

int main(int argc, char *argv[]) {
    int socket_desc, client_sock, c;
    struct sockaddr_in server, client;
    
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1) {
        perror("Could not create socket");
        return 1;
    }
    
    // Cho phép reuse address
    int opt = 1;
    setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(SERVER_PORT);
    
    if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Bind failed");
        return 1;
    }
    
    listen(socket_desc, 3);
    printf("MockServer is listening on port %d (Max clients: %d)...\n", SERVER_PORT, MAX_CLIENTS);
    
    c = sizeof(struct sockaddr_in);
    
    while ((client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c))) {
        if (client_count >= MAX_CLIENTS) {
            printf("MockServer: Max clients reached, rejecting connection\n");
            close(client_sock);
            continue;
        }
        
        printf("MockServer: Connection accepted (%d/%d clients)\n", client_count + 1, MAX_CLIENTS);
        pthread_t sniffer_thread;
        int *new_sock = (int*)malloc(sizeof(int));
        *new_sock = client_sock;
        
        if (pthread_create(&sniffer_thread, NULL, connection_handler, (void*)new_sock) < 0) {
            perror("Could not create thread");
            return 1;
        }
        
        pthread_detach(sniffer_thread);
    }
    
    if (client_sock < 0) {
        perror("Accept failed");
        return 1;
    }
    
    close(socket_desc);
    return 0;
}
