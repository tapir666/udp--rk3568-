#ifndef __GAME_2048_H__
#define __GAME_2048_H__

#include "../../lvgl/lvgl.h"

typedef enum DIR{UP=1, DOWN, LEFT, RIGHT}DIR;

void game_2048();
void init_2048_ui(void);
void draw_2048_on_screen(lv_obj_t *parent);

#endif