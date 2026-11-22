#ifndef DRONE_TRACKER_APP_BOARD_UI_H
#define DRONE_TRACKER_APP_BOARD_UI_H

#include "model.h"

int board_ui_init(void);
unsigned int board_ui_take_presses(void);
void board_ui_show(AppMode mode, TrackerState state);

#endif
