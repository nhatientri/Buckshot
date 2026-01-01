#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

#include "gui.h"
#include "protocol.h"
#include "gamestate.h" // Cần để truy cập g_GameState
#include "history_manager.h" // Cần để load_game_history

// Khai báo các widgets mới được thêm
GtkWidget *g_lbl_self_lives = NULL;
GtkWidget *g_lbl_opponent_lives = NULL;
GtkWidget *g_lbl_shell_count = NULL;
GtkWidget *g_box_items = NULL;

GtkWidget *g_btn_rematch = NULL;
GtkWidget *g_btn_accept_rematch = NULL; // Nút Accept rematch
GtkWidget *g_btn_reject_rematch = NULL; // Nút Reject rematch
GtkWidget *g_lbl_status = NULL;
GtkWidget *g_btn_resign = NULL; // Thêm lại vào global widgets để dễ điều khiển

// Định nghĩa Model cho GtkTreeView (History)
enum {
    COL_GAME_ID = 0,
    COL_OPPONENT,
    COL_RESULT,
    COL_ELO_NEW,
    COL_ELO_CHANGE,
    COL_TIMESTAMP,
    NUM_COLS
};

// --- 1. CALLBACKS CHO HÀNH ĐỘNG CLIENT (Nhiệm vụ: Resign/Rematch) ---

void on_resign_button_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    send_resign_game(); // Gửi lệnh GAME_RESIGN
    gtk_widget_set_sensitive(g_btn_resign, FALSE);
    if (g_lbl_status) {
        gtk_label_set_text(GTK_LABEL(g_lbl_status), "RESIGNED. Waiting for confirmation...");
    }
}

// Nhiệm vụ: Request rematch
void on_rematch_button_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    send_rematch_request(); // Gửi lệnh custom REMATCH_REQUEST
    gtk_widget_set_sensitive(g_btn_rematch, FALSE);
    if (g_lbl_status) {
        gtk_label_set_text(GTK_LABEL(g_lbl_status), "Request sent. Waiting for opponent...");
    }
}

// Các callback này không còn cần thiết vì đã dùng dialog popup
// Giữ lại để tương thích nhưng không sử dụng
void on_accept_rematch_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    // Không còn sử dụng - xử lý trong dialog popup
}

void on_reject_rematch_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    // Không còn sử dụng - xử lý trong dialog popup
}

void on_history_button_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    gui_show_game_history();
}

// --- 2. LOGIC VIEW HISTORY (Nhiệm vụ: Save game info and view history) ---

void gui_show_game_history() {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Game History");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 400);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(window), scrolled_window);

    GtkListStore *store;
    GtkWidget *tree_view;
    GtkTreeIter iter;

    // Khởi tạo Model
    store = gtk_list_store_new(NUM_COLS, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING);
    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_container_add(GTK_CONTAINER(scrolled_window), tree_view);

    // Thêm các cột (Columns)
    const char *column_titles[] = {"ID", "Opponent", "Result", "New ELO", "Change", "Timestamp"};
    for (int i = 0; i < NUM_COLS; i++) {
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
            column_titles[i], renderer, "text", i, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
    }

    // Đọc dữ liệu từ file CSV
    int count = 0;
    GameHistoryEntry *history = load_game_history(&count); 
    
    if (history) {
        // Điền dữ liệu vào Store
        for (int i = 0; i < count; i++) {
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter,
                COL_GAME_ID, history[i].game_id,
                COL_OPPONENT, history[i].opponent_name,
                COL_RESULT, history[i].result,
                COL_ELO_NEW, history[i].elo_new,
                COL_ELO_CHANGE, history[i].elo_change,
                COL_TIMESTAMP, history[i].timestamp,
                -1);
        }
        free(history); // Giải phóng bộ nhớ sau khi đã điền vào store
    } else {
        g_print("Error: Could not load game history.\n");
    }

    gtk_widget_show_all(window);
}


// --- 3. CÁC HÀM CẬP NHẬT GUI (được gọi từ protocol.c) ---

void gui_update_lives(int self_lives, int opponent_lives) {
    if (g_lbl_self_lives) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "Lives: %d", self_lives);
        gtk_label_set_text(GTK_LABEL(g_lbl_self_lives), buffer);
    }
    if (g_lbl_opponent_lives) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "Opponent: %d", opponent_lives);
        gtk_label_set_text(GTK_LABEL(g_lbl_opponent_lives), buffer);
    }
    // Logic cập nhật trạng thái turn (nếu có)
    if (g_lbl_status && g_btn_resign) {
        if (strcmp(g_GameState.turn, "YOURS") == 0) {
            gtk_label_set_text(GTK_LABEL(g_lbl_status), "YOUR TURN: ACT NOW!");
            gtk_widget_set_sensitive(g_btn_resign, TRUE);
        } else {
            gtk_label_set_text(GTK_LABEL(g_lbl_status), "OPPONENT'S TURN: WAITING...");
            gtk_widget_set_sensitive(g_btn_resign, FALSE);
        }
    }
}

void gui_update_shells(int live, int blank) {
    if (!g_lbl_shell_count) {
        return;
    }
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "Shells L/B: %d/%d", live, blank);
    gtk_label_set_text(GTK_LABEL(g_lbl_shell_count), buffer);
}

void gui_show_game_over(const char* result, int elo_change) {
    // Bỏ logic hiển thị dialog ở đây vì nó sẽ được gọi thông qua wrapper an toàn
    // Thay vào đó, ta gọi hàm hiển thị dialog trong wrapper an toàn (như đã hướng dẫn trong protocol.c)
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "%s (ΔELO: %+d)", // Sử dụng %+d để hiển thị dấu
        result ? result : "Result",
        elo_change);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    // Bật nút Rematch sau khi game kết thúc
    if (g_btn_rematch) {
        gtk_widget_set_sensitive(g_btn_rematch, TRUE);
    }
    if (g_lbl_status) {
        gtk_label_set_text(GTK_LABEL(g_lbl_status), "Game Over. Ready for rematch?");
    }
}

// Hiển thị rematch request từ đối thủ bằng dialog popup
void gui_show_rematch_request(const char* opponent_name) {
    // Tạo dialog popup mới
    GtkWidget *dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Rematch Request");
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    
    // Tạo nội dung dialog
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(NULL);
    
    char message[256];
    snprintf(message, sizeof(message), 
             "%s wants a rematch!\n\nDo you want to play again?",
             opponent_name ? opponent_name : "Opponent");
    gtk_label_set_text(GTK_LABEL(label), message);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    
    gtk_container_add(GTK_CONTAINER(content_area), label);
    
    // Thêm nút Accept và Reject
    GtkWidget *btn_accept = gtk_dialog_add_button(GTK_DIALOG(dialog), "Accept", GTK_RESPONSE_ACCEPT);
    GtkWidget *btn_reject = gtk_dialog_add_button(GTK_DIALOG(dialog), "Reject", GTK_RESPONSE_REJECT);
    
    // Thiết lập style cho nút
    gtk_widget_set_can_default(btn_accept, TRUE);
    gtk_widget_set_can_default(btn_reject, TRUE);
    
    // Hiển thị dialog
    gtk_widget_show_all(dialog);
    
    // Xử lý phản hồi từ dialog
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (response == GTK_RESPONSE_ACCEPT) {
        // Người dùng chọn Accept
        send_rematch_accept();
        if (g_lbl_status) {
            gtk_label_set_text(GTK_LABEL(g_lbl_status), "Rematch accepted. Starting new game...");
        }
    } else {
        // Người dùng chọn Reject hoặc đóng dialog
        send_rematch_reject();
        if (g_lbl_status) {
            gtk_label_set_text(GTK_LABEL(g_lbl_status), "Rematch rejected.");
        }
    }
    
    // Đóng dialog
    gtk_widget_destroy(dialog);
}

// Ẩn nút Accept/Reject rematch
void gui_hide_rematch_buttons() {
    if (g_btn_accept_rematch) {
        gtk_widget_set_visible(g_btn_accept_rematch, FALSE);
    }
    if (g_btn_reject_rematch) {
        gtk_widget_set_visible(g_btn_reject_rematch, FALSE);
    }
}

// Bật nút Resign sau khi match (khi có game_id)
void gui_enable_resign_button() {
    if (g_btn_resign) {
        gtk_widget_set_sensitive(g_btn_resign, TRUE);
    }
    if (g_lbl_status) {
        gtk_label_set_text(GTK_LABEL(g_lbl_status), "Game started. You can resign anytime.");
    }
}

// --- 4. XÂY DỰNG GIAO DIỆN CHÍNH (GUI - 3 ĐIỂM) ---

void create_main_window() {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Buckshot Roulette Online");
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    // Phần hiển thị game state
    g_lbl_self_lives = gtk_label_new("Lives: 0");
    g_lbl_opponent_lives = gtk_label_new("Opponent: 0");
    g_lbl_shell_count = gtk_label_new("Shells L/B: 0/0");
    g_lbl_status = gtk_label_new("Connecting...");
    g_box_items = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    gtk_box_pack_start(GTK_BOX(vbox), g_lbl_self_lives, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), g_lbl_opponent_lives, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), g_lbl_shell_count, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), g_lbl_status, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), g_box_items, FALSE, FALSE, 0);

    // --- Actions và Rematch/History Buttons ---
    GtkWidget *hbox_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    // Nút Resign
    g_btn_resign = gtk_button_new_with_label("Resign Game");
    g_signal_connect(g_btn_resign, "clicked", G_CALLBACK(on_resign_button_clicked), NULL);
    gtk_widget_set_sensitive(g_btn_resign, FALSE); 

    // Nút Rematch
    g_btn_rematch = gtk_button_new_with_label("Request Rematch");
    g_signal_connect(g_btn_rematch, "clicked", G_CALLBACK(on_rematch_button_clicked), NULL);
    gtk_widget_set_sensitive(g_btn_rematch, FALSE); 

    // Nút View History
    GtkWidget *btn_history = gtk_button_new_with_label("View Game History");
    g_signal_connect(btn_history, "clicked", G_CALLBACK(on_history_button_clicked), NULL); 

    // Không cần nút Accept/Reject trong cửa sổ chính nữa vì dùng dialog popup
    // Giữ lại biến để tương thích nhưng không thêm vào GUI
    g_btn_accept_rematch = NULL;
    g_btn_reject_rematch = NULL;

    gtk_box_pack_start(GTK_BOX(hbox_actions), g_btn_resign, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox_actions), g_btn_rematch, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox_actions), btn_history, TRUE, TRUE, 0);

    gtk_box_pack_end(GTK_BOX(vbox), hbox_actions, FALSE, FALSE, 10);
    
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(window);
}