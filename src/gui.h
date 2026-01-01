#ifndef GUI_H
#define GUI_H

#include <gtk/gtk.h>

// Định nghĩa các widget chính để có thể cập nhật trạng thái
extern GtkWidget *g_lbl_self_lives;
extern GtkWidget *g_lbl_opponent_lives;
extern GtkWidget *g_lbl_shell_count;
extern GtkWidget *g_box_items;

// Widget mới
extern GtkWidget *g_btn_rematch; 
extern GtkWidget *g_btn_accept_rematch; // Không còn sử dụng (giữ để tương thích)
extern GtkWidget *g_btn_reject_rematch; // Không còn sử dụng (giữ để tương thích)
extern GtkWidget *g_lbl_status;

// Hàm khởi tạo cửa sổ chính
void create_main_window();

// Callback mới
void on_resign_button_clicked(GtkWidget *widget, gpointer data);
void on_rematch_button_clicked(GtkWidget *widget, gpointer data);
void on_accept_rematch_clicked(GtkWidget *widget, gpointer data);
void on_reject_rematch_clicked(GtkWidget *widget, gpointer data);
void on_history_button_clicked(GtkWidget *widget, gpointer data);

// Các hàm được gọi từ protocol.c để cập nhật GUI
void gui_update_lives(int self_lives, int opponent_lives);
void gui_update_shells(int live, int blank);
void gui_show_game_over(const char* result, int elo_change);
void gui_show_rematch_request(const char* opponent_name); // Hiển thị dialog popup rematch request từ đối thủ
void gui_hide_rematch_buttons(); // Không còn cần thiết (giữ để tương thích)
void gui_enable_resign_button(); // Bật nút Resign sau khi match

// Hoàn thiện hàm View History
void gui_show_game_history(); 

#endif // GUI_H