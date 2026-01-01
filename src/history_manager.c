#include "history_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h> // Cần cho time() và localtime()

#define HISTORY_FILE "game_history.csv"
#define MAX_LINE_LEN 512

// Hàm cần thiết để phân tích chuỗi (đã được bao gồm trong string.h, nhưng cần chú ý reset)
// Hàm này là một hàm helper để phân tích từng trường dữ liệu.
char* get_next_token(char **str_ptr) {
    char *p = *str_ptr;
    char *q = p;
    if (!p) return NULL;

    while (*q && *q != ',') {
        q++;
    }

    if (*q == ',') {
        *q = '\0';
        *str_ptr = q + 1;
    } else {
        *str_ptr = NULL;
    }
    return p;
}

// Nhiệm vụ: Log game data & Save game info
void log_game_entry(int game_id, const char* result, int new_elo, int elo_change, const char* opponent) {
    FILE *fp;
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char buf[30];
    
    // Định dạng timestamp: YYYY-MM-DD HH:MM:SS
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);

    // Mở file để ghi (append)
    fp = fopen(HISTORY_FILE, "a");
    if (fp == NULL) {
        perror("Could not open history file");
        return;
    }

    // Ghi vào file CSV: ID, Opponent, Result, ELO_New, ELO_Change, Timestamp
    fprintf(fp, "%d,%s,%s,%d,%+d,%s\n", 
            game_id, opponent, result, new_elo, elo_change, buf);
    
    fclose(fp);
}


// Nhiệm vụ: View history
// Trả về con trỏ tới mảng GameHistoryEntry được cấp phát động (malloc)
GameHistoryEntry* load_game_history(int *count) {
    FILE *fp = fopen(HISTORY_FILE, "r");
    if (!fp) {
        *count = 0;
        return NULL;
    }

    char line[MAX_LINE_LEN];
    int capacity = 10;
    *count = 0;
    
    // Cấp phát bộ nhớ ban đầu
    GameHistoryEntry *history = (GameHistoryEntry*)malloc(capacity * sizeof(GameHistoryEntry));
    if (!history) {
        perror("Memory allocation failed");
        fclose(fp);
        return NULL;
    }

    while (fgets(line, MAX_LINE_LEN, fp)) {
        // Loại bỏ ký tự xuống dòng
        if (line[strlen(line) - 1] == '\n') {
            line[strlen(line) - 1] = '\0';
        }

        char *line_ptr = line;
        char *token;
        
        // Kiểm tra xem cần phải tăng kích thước mảng không
        if (*count >= capacity) {
            capacity *= 2;
            GameHistoryEntry *temp = (GameHistoryEntry*)realloc(history, capacity * sizeof(GameHistoryEntry));
            if (!temp) {
                perror("Memory reallocation failed");
                free(history);
                fclose(fp);
                *count = 0;
                return NULL;
            }
            history = temp;
        }

        // --- Phân tích CSV (Parsing) --- 
        
        // 1. Game ID
        token = get_next_token(&line_ptr);
        if (token) history[*count].game_id = atoi(token); else continue;

        // 2. Opponent Name
        token = get_next_token(&line_ptr);
        if (token) strncpy(history[*count].opponent_name, token, sizeof(history[*count].opponent_name) - 1); else continue;

        // 3. Result
        token = get_next_token(&line_ptr);
        if (token) strncpy(history[*count].result, token, sizeof(history[*count].result) - 1); else continue;

        // 4. New ELO
        token = get_next_token(&line_ptr);
        if (token) history[*count].elo_new = atoi(token); else continue;

        // 5. ELO Change (Có thể có dấu '+' nên cần cẩn thận)
        token = get_next_token(&line_ptr);
        if (token) {
            // Loại bỏ dấu '+' nếu có trước khi chuyển đổi
            history[*count].elo_change = atoi(token);
        } else continue;
        
        // 6. Timestamp (Phần còn lại của dòng)
        token = line_ptr;
        if (token) strncpy(history[*count].timestamp, token, sizeof(history[*count].timestamp) - 1); else continue;

        (*count)++;
    }

    fclose(fp);
    return history; // Người gọi hàm phải gọi free(history) sau khi sử dụng
}