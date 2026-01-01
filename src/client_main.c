#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <gtk/gtk.h>
#include "protocol.h"
#include "gamestate.h"
#include "gui.h"


// --- LOGIC XỬ LÝ SOCKET VÀ THREAD ---

// Hàm lắng nghe dữ liệu từ Server (chạy trong một thread riêng)
void* receive_data_thread(void* arg) {
    char buffer[4096];
    int bytes_received;

    while (g_client_socket > 0) {
        bytes_received = recv(g_client_socket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            // Gọi hàm xử lý tin nhắn (có thể cần xử lý các tin nhắn ghép/nhiều tin nhắn)
            handle_server_message(buffer);
        } else if (bytes_received == 0) {
            printf("Server closed connection.\n");
            break;
        } else {
            perror("recv failed");
            break;
        }
    }
    // Đảm bảo socket được đóng khi thread kết thúc
    if (g_client_socket > 0) {
        close(g_client_socket);
        g_client_socket = -1;
    }
    return NULL;
}

// Hàm chính của client
int main(int argc, char *argv[]) {
    // Thông tin server (Bạn cần biết địa chỉ IP và Port của đồng đội)
    char *server_ip = "127.0.0.1"; // CHỖ NÀY CẦN CODE CỦA BẠN (IP của Server)
    int server_port = 8080;        // CHỖ NÀY CẦN CODE CỦA BẠN (Port của Server)
    
    struct sockaddr_in server_addr;
    pthread_t recv_thread;

    gtk_init(&argc, &argv);

    // 1. Tạo Socket
    g_client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_client_socket == -1) {
        perror("Could not create socket");
        return 1;
    }

    // 2. Thiết lập cấu trúc Server
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(g_client_socket);
        return 1;
    }

    // 3. Kết nối
    if (connect(g_client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection Failed");
        close(g_client_socket);
        return 1;
    }
    printf("Connected to server %s:%d\n", server_ip, server_port);

    // 4. Khởi tạo Thread nhận dữ liệu
    if (pthread_create(&recv_thread, NULL, receive_data_thread, NULL) != 0) {
        perror("Could not create receive thread");
        close(g_client_socket);
        return 1;
    }

    create_main_window();
    send_login("my_user", "my_hashed_pass"); // Hàm giả định để kiểm tra

    // Giữ main thread chạy để thread nhận dữ liệu không bị hủy
    gtk_main(); 

    return 0;
}

// Hàm giả định để gửi LOGIN (chỉ dùng cho main.c test)
void send_login(const char* username, const char* pass_hash) {
    struct json_object *payload = json_object_new_object();
    json_object_object_add(payload, "username", json_object_new_string(username));
    json_object_object_add(payload, "pass_hash", json_object_new_string(pass_hash));
    send_message("LOGIN", payload);
}