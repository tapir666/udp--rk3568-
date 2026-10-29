#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H

#include "../../lvgl/lvgl.h"

void draw_file_transfer_on_screen(lv_obj_t *parent);
void file_transfer_timer_cb(lv_timer_t *timer);

#endif
