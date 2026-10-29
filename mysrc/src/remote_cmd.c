#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "../../lvgl/lvgl.h"
#include "../inc/remote_cmd.h"
#include "../inc/rdt_core.h"

//引用主界面和各个游戏界面
extern lv_obj_t *main_src;
extern lv_obj_t *a_src, *b_src, *c_src, *d_src, *e_src, *f_src;

//引用播放器控制接口
extern void mplayer_stop(void);
extern int mplayer_start(char *file_name);
extern void mplayer_play_if_idle(void);
extern void mplayer_pause(void);
extern void mplayer_set_volume(int vol);

//---------- 命令队列：网络线程投递，LVGL主线程消费 ----------
#define CMD_QUEUE_MAX 32
#define CMD_LEN_MAX 256
static char cmd_queue[CMD_QUEUE_MAX][CMD_LEN_MAX];
static int cmd_head = 0;
static int cmd_tail = 0;
static int cmd_count = 0;
static pthread_mutex_t cmd_lock = PTHREAD_MUTEX_INITIALIZER;

static void post_command(const char *cmd)
{
    pthread_mutex_lock(&cmd_lock);
    if(cmd_count < CMD_QUEUE_MAX)
    {
        snprintf(cmd_queue[cmd_tail], CMD_LEN_MAX, "%s", cmd);
        cmd_tail = (cmd_tail + 1) % CMD_QUEUE_MAX;
        cmd_count++;
    }
    pthread_mutex_unlock(&cmd_lock);
}

//在LVGL主线程执行远程命令
static void remote_ui_handle(const char *cmd)
{
    if(strcmp(cmd, "menu") == 0)
    {
        mplayer_stop();
        lv_screen_load(main_src);
    }
    else if(strcmp(cmd, "game whack") == 0)
    {
        mplayer_stop();
        lv_screen_load(a_src);
    }
    else if(strcmp(cmd, "game chess") == 0)
    {
        mplayer_stop();
        lv_screen_load(b_src);
    }
    else if(strcmp(cmd, "game 2048") == 0)
    {
        mplayer_stop();
        lv_screen_load(c_src);
    }
    else if(strcmp(cmd, "game sanguosha") == 0)
    {
        mplayer_stop();
        lv_screen_load(d_src);
    }
    else if(strcmp(cmd, "game transfer") == 0 ||
            strcmp(cmd, "filetransfer") == 0)
    {
        mplayer_stop();
        lv_screen_load(e_src);
    }
    else if(strcmp(cmd, "video") == 0)
    {
        mplayer_play_if_idle();
        lv_screen_load(f_src);
    }
    else if(strncmp(cmd, "play ", 5) == 0)
    {
        mplayer_stop();
        mplayer_start((char *)cmd + 5);
    }
    else if(strcmp(cmd, "pause") == 0)
    {
        mplayer_pause();
    }
    else if(strcmp(cmd, "stop") == 0)
    {
        mplayer_stop();
    }
    else if(strncmp(cmd, "volume ", 7) == 0)
    {
        mplayer_set_volume(atoi(cmd + 7));
    }
}

void remote_cmd_timer_cb(lv_timer_t *timer)
{
    char cmd[CMD_LEN_MAX];
    while(1)
    {
        pthread_mutex_lock(&cmd_lock);
        if(cmd_count == 0)
        {
            pthread_mutex_unlock(&cmd_lock);
            return;
        }
        snprintf(cmd, sizeof(cmd), "%s", cmd_queue[cmd_head]);
        cmd_head = (cmd_head + 1) % CMD_QUEUE_MAX;
        cmd_count--;
        pthread_mutex_unlock(&cmd_lock);

        remote_ui_handle(cmd);
    }
}

int remote_cmd_init(int port)
{
    return rdt_server_multi_start(port, post_command);
}
