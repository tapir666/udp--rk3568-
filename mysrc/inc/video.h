#ifndef __VIDEO_H__
#define __VIDEO_H__

#include "../../lvgl/lvgl.h"

/* ---------- 播放列表 ---------- */
static char *playlist[] = {
    "/tapir/video/1.mp4",
    "/tapir/video/2.mp4",
};
void mplayer_ui(lv_obj_t *parent);
int mplayer_start(char *file_name);
static void update_list_highlight(void);
int mplayer_start(char *file_name);
void mplayer_ui(lv_obj_t *parent);
void mplayer_stop(void);
void mplayer_play_if_idle(void);
void mplayer_pause(void);
void mplayer_set_volume(int vol);
void video_playlist_init(void);

typedef enum {
    PLAYER_STOPPED = 0,
    PLAYER_PLAYING,
    PLAYER_PAUSED
} player_state_t;

typedef enum{
    PLAY_MODE_AUTO,
    PLAY_MODE_SINGLE,
}player_mode_t;

#endif
