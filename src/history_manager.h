#ifndef HISTORY_MANAGER_H
#define HISTORY_MANAGER_H

#include "gamestate.h"
#include <time.h>

void log_game_entry(int game_id, const char* result, int new_elo, int elo_change, const char* opponent);
GameHistoryEntry* load_game_history(int *count);

#endif // HISTORY_MANAGER_H