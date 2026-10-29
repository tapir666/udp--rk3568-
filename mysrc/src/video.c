#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>    
#include <pthread.h>
#include <sys/wait.h>
#include "video.h"
#include "../../lvgl/lvgl.h"
#include "mysrc/inc/linklist.h"

pid_t mplayer_pid;
int mplayer_fd = -1;


#define MAX_VIDEOS 32

static lv_obj_t *stop_btn = NULL;
static lv_obj_t *prev_btn = NULL;
static lv_obj_t *next_btn = NULL;
static lv_obj_t *up_btn = NULL;
static lv_obj_t *down_btn = NULL;
static lv_obj_t *mode_btn = NULL;
static lv_obj_t *fs_btn = NULL;
static lv_obj_t *fs_tap_area = NULL;

static lv_obj_t *video_list = NULL;
static Node *list_nodes[MAX_VIDEOS];
static lv_obj_t *list_btns[MAX_VIDEOS];
static int list_count = 0;
static int video_fullscreen = 0;   // 0=窗口 800x450，1=全宽 1024x600

static double current_speed = 1.0;   // 当前播放速度
static const double speed_table[] = { 0.5, 1.0, 1.5, 2.0, 3.0 };  // 可选档位
static int speed_idx = 1;           // 当前在 speed_table 中的索引，默认 1.0x
static lv_obj_t *speed_label = NULL;  //用于显示当前速度

static int mplayer_out_fd = -1;   // mplayer stdout 读端
pthread_t out_tid;                // 输出解析线程

static lv_obj_t *volume_slider = NULL;
static int current_volume = 80;   // 默认音量，跨视频保留
static lv_obj_t *mode_label = NULL; //播放模式

typedef struct {
    lv_obj_t *status_label; //状态标签
    lv_obj_t *slider;       //进度条控件
    lv_obj_t *time_label;   //进度条上面的时间显示标签
    int updating_progress;  //是否正在更新进度
    int current_position;   // 当前播放位置
    int restarting;  
    int dragging;           //拖动标志，在拖动为1，松开为0
    struct {
        player_state_t state;
        double speed;
        int duration;       // 秒
        player_mode_t play_mode;
    } player;
} player_ui_t;

static player_ui_t ui = {0};
static double player_speed = 1.0; 

pthread_t tid; //检测是否自动播放结束的线程
extern Node *play;

static Linklist *video_head = NULL;   // 存住头指针，防止链表被回收

void video_playlist_init(void)
{
    video_head = create_linklist();
    save_path(video_head, "/tapir/video");
    play = video_head->first;
}

static void volume_slider_cb(lv_event_t * e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int vol = lv_slider_get_value(slider);
    mplayer_set_volume(vol);
}

static void update_status(const char *text);

void mplayer_pause(void)
{
    if(mplayer_fd == -1)
        return;

    char cmd[] = "pause\n";
    write(mplayer_fd, cmd, strlen(cmd));
    if(ui.player.state == PLAYER_PLAYING)
    {
        ui.player.state = PLAYER_PAUSED;
        update_status("Paused");
    }
    else if(ui.player.state == PLAYER_PAUSED)
    {
        ui.player.state = PLAYER_PLAYING;
        update_status("Resumed");
    }
}

void mplayer_set_volume(int vol)
{
    if(vol < 0) vol = 0;
    if(vol > 100) vol = 100;

    current_volume = vol;

    //同步LVGL滑动条
    if(volume_slider != NULL)
        lv_slider_set_value(volume_slider, vol, LV_ANIM_OFF);

    if(mplayer_fd != -1)
    {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "volume %d 1\n", vol);
        write(mplayer_fd, cmd, strlen(cmd));
    }
}

static int mplayer_switch_to(Node *node);

void *judge_mplayer_auto_end(void *arg)
{
    pid_t wait_pid = waitpid(mplayer_pid, NULL, 0); //阻塞回收子进程
    if(wait_pid == mplayer_pid)
    {
        if(mplayer_fd != -1)
        {
            close(mplayer_fd);
            mplayer_fd = -1;
        }

        mplayer_pid = -1;
        if(ui.player.play_mode == PLAY_MODE_SINGLE)
        {
            mplayer_start(play->data);
        }else
        {
            play = play->next;
            mplayer_start(play->data);
        }
    }
    return NULL;
}


static void playlist_item_cb(lv_event_t * e)
{
    Node *target = (Node *)lv_event_get_user_data(e);
    if (target == NULL || target == play) return;   // 点当前视频就不动

    play = target;
    mplayer_switch_to(play);
    update_list_highlight();
}

static void create_video_list(lv_obj_t *parent)
{
    if (play == NULL) return;                 // 链表没建好就先不建列表

    video_list = lv_list_create(parent);
    lv_obj_set_size(video_list, 224, 450);    // 左边栏，和视频区域同高
    lv_obj_set_pos(video_list, 0, 0);

    list_count = 0;
    Node *p = play;
    do {
        if (list_count >= MAX_VIDEOS) break;

        // 只显示文件名，不显示完整路径
        const char *name = p->data;
        char *slash = strrchr(name, '/');
        if (slash) name = slash + 1;

        lv_obj_t *btn = lv_list_add_btn(video_list, LV_SYMBOL_VIDEO, name);
        lv_obj_add_event_cb(btn, playlist_item_cb, LV_EVENT_CLICKED, p);

        list_nodes[list_count] = p;
        list_btns[list_count] = btn;
        list_count++;

        p = p->next;
    } while (p != play);                      // 循环链表遍历一圈
}

static void update_list_highlight(void)
{
    if (video_list == NULL) return;
    for (int i = 0; i < list_count; i++) {
        if (list_btns[i] == NULL) continue;
        if (list_nodes[i] == play)
            lv_obj_add_state(list_btns[i], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(list_btns[i], LV_STATE_CHECKED);
    }
}


static void *mplayer_output_reader(void *arg)
{
    char buf[512];
    static char line_buf[256];
    static int line_len = 0;
    ssize_t n;

    while ((n = read(mplayer_out_fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') {
                line_buf[line_len] = '\0';
                if (strncmp(line_buf, "ID_LENGTH=", 10) == 0) {
                    ui.player.duration = (int)(atof(line_buf + 10) + 0.5);
                } else if (strncmp(line_buf, "ANS_LENGTH=", 11) == 0) {
                    ui.player.duration = (int)(atof(line_buf + 11) + 0.5);
                } else if (strncmp(line_buf, "ANS_TIME_POSITION=", 18) == 0) {
                    ui.current_position = (int)atof(line_buf + 18);
                }
                line_len = 0;
            } else if (line_len < (int)sizeof(line_buf) - 1) {
                line_buf[line_len++] = c;
            }
        }
    }

    close(mplayer_out_fd);
    mplayer_out_fd = -1;
    return NULL;
}

// // 获取视频时长
// static int get_video_duration(const char *filepath)
// {
//     char command[512];                                                                                                       //命令
//     snprintf(command, sizeof(command),                                                                                       //格式化命令
//              "/tapir/mplayer64 -identify -frames 0 -vo null -ao null \"%s\" 2>/dev/null | grep 'ID_LENGTH' | awk -F'=' '{print $2}'", //获取视频时长
//              filepath);

//     FILE *fp = popen(command, "r"); //打开命令
//     if (!fp)
//         return 300; //如果打开失败，返回300

//     char duration_str[32] = {0}; //时长字符串
//     if (fgets(duration_str, sizeof(duration_str), fp) == NULL)
//     {               //如果获取失败，返回300
//         pclose(fp); //关闭文件
//         return 300; //返回300
//     }
//     pclose(fp); //关闭文件

//     char *newline = strchr(duration_str, '\n'); //换行符
//     if (newline)
//         *newline = '\0'; //如果换行符不为空，则设置为空

//     float duration = atof(duration_str);                  //转换为浮点数
//     return (duration <= 0) ? 300 : (int)(duration + 0.5); //如果时长小于等于0，返回300，否则返回时长
// }

int mplayer_start(char *file_name)
{
    char speed_arg[16];
    snprintf(speed_arg, sizeof(speed_arg), "%.1f", player_speed);  // fork 前准备好

    int pipefd[2];      // 父 -> 子 stdin（控制命令）
    int out_pipe[2];    // 子 stdout -> 父（回包）
    if (pipe(pipefd) == -1 || pipe(out_pipe) == -1) {
        perror("pipe fail");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork fail");
        return -1;
    }
    else if (pid == 0)
    {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(out_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);

        if (video_fullscreen) {
            execl("/tapir/mplayer64", "mplayer64",
              "-slave", "-quiet", "-identify",
              "-vo", "fbdev2", "-ao", "alsa",
              "-geometry", "0:0", "-zoom", "-x", "1024", "-y", "600",
              "-speed", speed_arg, "-framedrop",
              file_name, NULL);
        } else {
                execl("/tapir/mplayer64", "mplayer64",
                "-slave", "-quiet", "-identify",
                "-vo", "fbdev2", "-ao", "alsa",
                "-geometry", "224:0", "-zoom", "-x", "800", "-y", "450",
                "-speed", speed_arg, "-framedrop",
                file_name, NULL);
            }
            perror("execl fail");
            exit(1);
            
    }

    close(pipefd[0]);
    close(out_pipe[1]);
    mplayer_pid = pid;
    mplayer_fd = pipefd[1];
    mplayer_out_fd = out_pipe[0];

    char vol_cmd[32];
    snprintf(vol_cmd, sizeof(vol_cmd), "volume %d 1\n", current_volume);
    write(mplayer_fd, vol_cmd, strlen(vol_cmd));

    ui.player.state = PLAYER_PLAYING;
    ui.current_position = 0;
    ui.player.speed = player_speed;

    // 先启动两个线程，再取时长
    pthread_create(&tid, NULL, judge_mplayer_auto_end, NULL);
    pthread_create(&out_tid, NULL, mplayer_output_reader, NULL);
    return 0;
}

/*
    mplayer_switch_to: 停止当前播放，切换到 node 节点中 data 视频
    返回值: 同 mplayer_start
*/
static int mplayer_switch_to(Node *node)
{
    if (node == NULL) return -1;

    // 先停输出解析线程，再停监控线程
    if (mplayer_out_fd != -1) {
        pthread_cancel(out_tid);
        pthread_join(out_tid, NULL);
    }
    if (mplayer_pid > 0) {
        pthread_cancel(tid);
        pthread_join(tid, NULL);
    }

    if (mplayer_pid > 0) {
        if (mplayer_fd != -1)
            write(mplayer_fd, "quit\n", 5);
        kill(mplayer_pid, SIGTERM);
        waitpid(mplayer_pid, NULL, 0);
    }
    if (mplayer_fd != -1) {
        close(mplayer_fd);
        mplayer_fd = -1;
    }
    if (mplayer_out_fd != -1) {
        close(mplayer_out_fd);
        mplayer_out_fd = -1;
    }
    mplayer_pid = -1;

    return mplayer_start(node->data);
}

static void update_progress_display(void);

static void seek_to(int second)
{
    if (mplayer_fd == -1 || ui.player.duration <= 0) return;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "seek %d 2\n", second);   // 2 = 绝对秒数
    write(mplayer_fd, cmd, strlen(cmd));

    ui.current_position = second;
    update_progress_display();
}

static void update_fullscreen_ui(void)
{
    lv_obj_t *objs[] = {
        video_list,
        stop_btn,
        prev_btn,
        next_btn,
        up_btn,
        down_btn,
        speed_label,
        mode_btn,
        mode_label,
        volume_slider,
        ui.slider,
        ui.time_label,
        ui.status_label,
        fs_btn,
    };

    for (size_t i = 0; i < sizeof(objs) / sizeof(objs[0]); i++) {
        if (objs[i] == NULL) continue;
        if (video_fullscreen)
            lv_obj_add_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_clear_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (fs_tap_area != NULL) {
        if (video_fullscreen) {
            lv_obj_clear_flag(fs_tap_area, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(fs_tap_area);
        }
        else
            lv_obj_add_flag(fs_tap_area, LV_OBJ_FLAG_HIDDEN);
    }
}

static void fullscreen_button_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    int saved_pos = ui.current_position;   // 记住播放位置
    video_fullscreen = !video_fullscreen;
    update_fullscreen_ui();

    // 全屏时左侧视频列表会被视频盖住，藏起来
    if (video_list) {
        if (video_fullscreen)
            lv_obj_add_flag(video_list, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_clear_flag(video_list, LV_OBJ_FLAG_HIDDEN);
    }

    if (play != NULL && mplayer_pid > 0) {
        mplayer_stop();
        mplayer_start(play->data);

        if (video_fullscreen && mplayer_fd != -1) {
            char osd_cmd[] = "osd_show_text \"Tap to exit fullscreen\" 5000 2\n";
            write(mplayer_fd, osd_cmd, strlen(osd_cmd));
        }

        // 跳回切换前的位置（mplayer 会落到最近的关键帧，差几秒正常）
        if (saved_pos > 0) {
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "seek %d 2\n", saved_pos);
            write(mplayer_fd, cmd, strlen(cmd));
            ui.current_position = saved_pos;
        }
        update_progress_display();
    }
}

static void slider_event_cb(lv_event_t * e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    int target = (int)((double)val / 100.0 * ui.player.duration + 0.5);

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSING) {
        ui.dragging = 1;
        ui.current_position = target;        // 拖动时实时更新时间
        update_progress_display();
    } else if (code == LV_EVENT_RELEASED) {
        ui.dragging = 0;
        seek_to(target);                     // 松手才真正跳转
    }
}

void prev_button_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        play = play->prev;
        mplayer_switch_to(play);
    }
}

void next_button_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        play = play->next;
        mplayer_switch_to(play);
    }
}

static void update_status(const char *text);

void stop_button_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED && mplayer_fd != -1)
    {
        char cmd[] = "pause\n";
        write(mplayer_fd, cmd, strlen(cmd));
        if (ui.player.state == PLAYER_PLAYING) {
            ui.player.state = PLAYER_PAUSED;
            update_status("Paused");
        } 
        else if (ui.player.state == PLAYER_PAUSED) {
            ui.player.state = PLAYER_PLAYING;
            update_status("Resumed");
        }

    }
}


/* ---- 倍速 + ：升一档 ---- */
void speed_up_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED || mplayer_fd == -1) return;

    if (speed_idx < 4) {   // 上限 3.0x  4是speed_table 最大下标
        speed_idx++;
        current_speed = speed_table[speed_idx];
        player_speed = current_speed;
        ui.player.speed = current_speed;  
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "speed_set %.1f\n", current_speed);
        write(mplayer_fd, cmd, strlen(cmd));

        if (speed_label)
        {
            char speed_str[16];
            snprintf(speed_str, sizeof(speed_str), "%.1fx", current_speed);
            lv_label_set_text(speed_label, speed_str);
        }

    }
}

/* ---- 倍速 - ：降一档 ---- */
void speed_down_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED || mplayer_fd == -1) return;

    if (speed_idx > 0) {   // 下限 0.5x
        speed_idx--;
        current_speed = speed_table[speed_idx];
        player_speed = current_speed;
        ui.player.speed = current_speed;  
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "speed_set %.1f\n", current_speed);
        write(mplayer_fd, cmd, strlen(cmd));

        if (speed_label)
        {
            char speed_str[16];
            snprintf(speed_str, sizeof(speed_str), "%.1fx", current_speed);
            lv_label_set_text(speed_label, speed_str);
        }
    }
}

static void mode_button_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    if (ui.player.play_mode == PLAY_MODE_AUTO) {
        ui.player.play_mode = PLAY_MODE_SINGLE;
        update_status("Mode: Single loop");
    } else {
        ui.player.play_mode = PLAY_MODE_AUTO;
        update_status("Mode: List loop");
    }

    if (mode_label)
        lv_label_set_text(mode_label,
            ui.player.play_mode == PLAY_MODE_AUTO ? "List loop" : "Single loop");
}

static void hide_status_label(lv_timer_t *timer)
{
    if (ui.status_label)
        lv_obj_add_flag(ui.status_label, LV_OBJ_FLAG_HIDDEN);
    lv_timer_delete(timer);
}

static void update_status(const char *text)
{
    if (ui.status_label == NULL) return;
    lv_label_set_text(ui.status_label, text);
    lv_timer_t *timer = lv_timer_create(hide_status_label, 2000, NULL);
    lv_timer_set_repeat_count(timer, 1);
}

static void update_progress_display(void)
{
    if (ui.updating_progress || ui.restarting) return;
    ui.updating_progress = 1;
    lv_slider_set_value(ui.slider, 
        ui.player.duration > 0 ? (int)((double)ui.current_position / ui.player.duration * 100) : 0,
        LV_ANIM_OFF);

    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d/%02d:%02d",
             ui.current_position / 60, ui.current_position % 60,
             ui.player.duration / 60, ui.player.duration % 60);
    lv_label_set_text(ui.time_label, time_str);
    ui.updating_progress = 0;
}

static void progress_timer_cb(lv_timer_t *timer)
{
    if (ui.player.state != PLAYER_PLAYING || ui.restarting || ui.dragging)
        return;

    if (mplayer_fd != -1)
        write(mplayer_fd, "get_time_pos\n", 13);
    update_progress_display();
    update_list_highlight();  
}

void mplayer_stop(void)
{
    if (mplayer_out_fd != -1) {
        pthread_cancel(out_tid);
        pthread_join(out_tid, NULL);
    }
    if (mplayer_pid > 0) {
        pthread_cancel(tid);
        pthread_join(tid, NULL);
        if (mplayer_fd != -1)
            write(mplayer_fd, "quit\n", 5);
        kill(mplayer_pid, SIGTERM);
        waitpid(mplayer_pid, NULL, 0);
    }
    if (mplayer_fd != -1) {
        close(mplayer_fd);
        mplayer_fd = -1;
    }
    if (mplayer_out_fd != -1) {
        close(mplayer_out_fd);
        mplayer_out_fd = -1;
    }
    mplayer_pid = -1;
}

void mplayer_play_if_idle(void)
{
    if (play != NULL && mplayer_pid <= 0)
        mplayer_start(play->data);
}

void mplayer_ui(lv_obj_t *parent)   // 应该是 mplayer_ui
{

    ui.player.speed = 1;
    ui.player.play_mode = PLAY_MODE_AUTO;

    /* ---- 已存在的停止按钮 ---- */
    stop_btn = lv_button_create(parent);
    lv_obj_set_size(stop_btn, 80, 80);
    lv_obj_set_pos(stop_btn, 600, 480);
    lv_obj_add_event_cb(stop_btn, stop_button_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, LV_SYMBOL_PAUSE);
    lv_obj_center(stop_label);

    /* ---- 上一个视频按钮 ---- */
    prev_btn = lv_button_create(parent);
    lv_obj_set_size(prev_btn, 80, 80);
    lv_obj_set_pos(prev_btn, 500, 480);
    lv_obj_add_event_cb(prev_btn, prev_button_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *prev_label = lv_label_create(prev_btn);
    lv_label_set_text(prev_label, LV_SYMBOL_PREV);
    lv_obj_center(prev_label);

    /* ---- 下一个视频按钮 ---- */
    next_btn = lv_button_create(parent);
    lv_obj_set_size(next_btn, 80, 80);
    lv_obj_set_pos(next_btn, 700, 480);
    lv_obj_add_event_cb(next_btn, next_button_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *next_label = lv_label_create(next_btn);
    lv_label_set_text(next_label, LV_SYMBOL_NEXT);
    lv_obj_center(next_label);

    /* ---- 速度显示 ---- */
    speed_label = lv_label_create(parent);
    char speed_str[16];
    snprintf(speed_str, sizeof(speed_str), "%.1fx", current_speed);
    lv_label_set_text(speed_label, speed_str);
    lv_obj_set_style_text_color(speed_label,lv_color_hex(0x000000),0);
    lv_obj_set_style_text_font(speed_label,&lv_font_montserrat_30,0);
    lv_obj_set_pos(speed_label, 950, 480);

    /* ---- 加速按钮 ---- */
    up_btn = lv_button_create(parent);
    lv_obj_set_size(up_btn,80,80);
    lv_obj_set_pos(up_btn, 800, 480);
    lv_obj_add_event_cb(up_btn, speed_up_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * up_lbl = lv_label_create(up_btn);
    lv_label_set_text(up_lbl, LV_SYMBOL_PLUS);
    lv_obj_center(up_lbl);

    /* --- 减速按钮 --- */
    down_btn = lv_button_create(parent);
    lv_obj_set_size(down_btn, 80, 80);
    lv_obj_set_pos(down_btn, 400, 480);
    lv_obj_add_event_cb(down_btn, speed_down_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * down_lbl = lv_label_create(down_btn);
    lv_label_set_text(down_lbl, LV_SYMBOL_MINUS);
    lv_obj_center(down_lbl);

    /* --- 状态标签，显示当前模式 --- */
    ui.status_label = lv_label_create(parent);
    lv_label_set_text(ui.status_label,"");
    lv_obj_set_pos(ui.status_label,850,480);
    lv_obj_add_flag(ui.status_label,LV_OBJ_FLAG_HIDDEN);

    /* --- 显示当前进度 --- */
    ui.time_label = lv_label_create(parent);
    lv_label_set_text(ui.time_label,"00:00/00:00");
    lv_obj_set_pos(ui.time_label,200,480);

    /* --- 滑动条控件 --- */    
    ui.slider = lv_slider_create(parent);
    lv_obj_set_size(ui.slider,800,30);
    lv_obj_set_pos(ui.slider,224,450);
    lv_slider_set_range(ui.slider,0,100);
    
    lv_obj_add_event_cb(ui.slider, slider_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(ui.slider, slider_event_cb, LV_EVENT_RELEASED, NULL);
    
    /* --- 定时器控件 --- */    
    lv_timer_create(progress_timer_cb,1000,NULL);

    create_video_list(parent);

    /* --- 音量滑块 --- */
    lv_obj_t * vol_label = lv_label_create(parent);
    lv_label_set_text(vol_label, "VOL");
    lv_obj_set_pos(vol_label, 10, 465);

    /* --- 播放模式切换按钮 --- */
    mode_btn = lv_button_create(parent);
    lv_obj_set_size(mode_btn, 190, 40);
    lv_obj_set_pos(mode_btn, 10, 550);
    lv_obj_add_event_cb(mode_btn, mode_button_cb, LV_EVENT_CLICKED, NULL);

    mode_label = lv_label_create(mode_btn);
    lv_label_set_text(mode_label, "List loop");   // 初始和默认模式一致
    lv_obj_center(mode_label);

    volume_slider = lv_slider_create(parent);
    lv_obj_set_size(volume_slider, 190, 20);
    lv_obj_set_pos(volume_slider, 10, 490);
    lv_slider_set_range(volume_slider, 0, 100);
    lv_slider_set_value(volume_slider, current_volume, LV_ANIM_OFF);
    lv_obj_add_event_cb(volume_slider, volume_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* --- 全屏切换按钮 --- */
    fs_btn = lv_button_create(parent);
    lv_obj_set_size(fs_btn, 190, 30);
    lv_obj_set_pos(fs_btn, 10, 515);
    lv_obj_add_event_cb(fs_btn, fullscreen_button_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * fs_label = lv_label_create(fs_btn);
    lv_label_set_text(fs_label, "Fullscreen");
    lv_obj_center(fs_label);

    /* Transparent tap area that covers the whole screen in fullscreen mode. */
    fs_tap_area = lv_obj_create(parent);
    lv_obj_set_size(fs_tap_area, 1024, 600);
    lv_obj_set_pos(fs_tap_area, 0, 0);
    lv_obj_set_style_bg_opa(fs_tap_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fs_tap_area, 0, 0);
    lv_obj_add_flag(fs_tap_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(fs_tap_area, fullscreen_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(fs_tap_area, LV_OBJ_FLAG_HIDDEN);

    update_fullscreen_ui();
}

