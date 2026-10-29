#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "whack_a_hole.h"
#include "../lv_ls_font_24.h"
#include "../../lvgl/lvgl.h"

#define GRID_SIZE 3
#define HOLE_R 60
#define GAME_TIME 30
#define PIC_MOLE_PATH "A:/tapir/bmp/bgf.bmp"

static lv_obj_t *container = NULL;
static lv_obj_t *holes[GRID_SIZE][GRID_SIZE];
static lv_obj_t *hole_labels[GRID_SIZE][GRID_SIZE];
static lv_obj_t *score_label = NULL;
static lv_obj_t *time_label = NULL;
static lv_obj_t *result_label = NULL;
static int active_hole[GRID_SIZE][GRID_SIZE];
static int score = 0;
static int time_left = GAME_TIME;
static int game_running = 0;
static lv_timer_t *game_clock = NULL;
static lv_timer_t *mole_spawner = NULL;
static lv_obj_t   *bmp[GRID_SIZE][GRID_SIZE];
static lv_obj_t   *bg_bmp;
static lv_obj_t *start_btn = NULL;
// static lv_style_t bmp_style[GRID_SIZE][GRID_SIZE]; 

//隐藏洞口老鼠
static void hide_mole(int r, int c)
{
    active_hole[r][c] = 0;
    lv_obj_set_style_bg_color(holes[r][c], lv_color_hex(0x8B4513), 0);
    // lv_label_set_text(hole_labels[r][c], "");
    lv_obj_add_flag(bmp[r][c], LV_OBJ_FLAG_HIDDEN);
}


//生成一只新老鼠
static void spawn_mole(lv_timer_t *timer)
{
    if (!game_running) return;
    //寻找没有老鼠的洞
    int available[GRID_SIZE * GRID_SIZE][2];
    int count = 0;
    for (int r = 0; r < GRID_SIZE; r++) {
        for (int c = 0; c < GRID_SIZE; c++) {
            if (!active_hole[r][c]) {
                available[count][0] = r;
                available[count][1] = c;
                count++;
            }
        }
    }
    //全都有老鼠，返回
    if (count == 0) return;
    //随机选一个洞
    int idx = rand() % count;
    int r = available[idx][0];
    int c = available[idx][1];
    //显示老鼠图片，并标记该洞有老鼠
    active_hole[r][c] = 1;
    lv_obj_set_style_bg_color(holes[r][c], lv_color_hex(0xDAA520), 0);
    // lv_label_set_text(hole_labels[r][c], "shu");
    lv_obj_remove_flag(bmp[r][c], LV_OBJ_FLAG_HIDDEN);
}

//打地鼠点击回调函数并更新分数
static void hole_click_cb(lv_event_t *e)
{
    if (!game_running) return;

    lv_obj_t *btn = lv_event_get_target(e);
    for (int r = 0; r < GRID_SIZE; r++) {
        for (int c = 0; c < GRID_SIZE; c++) {
            if (holes[r][c] == btn && active_hole[r][c]) {
                active_hole[r][c] = 0;
                score++;
                hide_mole(r, c);

                char buf[32];
                snprintf(buf, sizeof(buf), "Score: %d", score);
                lv_label_set_text(score_label, buf);
                return;
            }
        }
    }
}

static void result_click_cb(lv_event_t *e);

//检测游戏是否结束，更新时间并随机消失一部分地鼠
static void game_tick(lv_timer_t *timer)
{
    if (!game_running) return;

    time_left--;
    char buf[32];
    snprintf(buf, sizeof(buf), "Time: %ds", time_left);
    lv_label_set_text(time_label, buf);

    for (int r = 0; r < GRID_SIZE; r++) {
        for (int c = 0; c < GRID_SIZE; c++) {
            if (active_hole[r][c] && (rand() % 3 == 0))
                hide_mole(r, c);
        }
    }

    if (time_left <= 0) {
        game_running = 0;
        if (game_clock) { lv_timer_del(game_clock); game_clock = NULL; }
        if (mole_spawner) { lv_timer_del(mole_spawner); mole_spawner = NULL; }

        for (int r = 0; r < GRID_SIZE; r++)
            for (int c = 0; c < GRID_SIZE; c++)
                if (active_hole[r][c]) hide_mole(r, c);

        char result[64];
        snprintf(result, sizeof(result), "Game Over!\nScore: %d\nTap to restart", score);
        result_label = lv_label_create(container);
        lv_label_set_text(result_label, result);
        lv_obj_set_style_text_font(result_label, &lv_font_montserrat_48, 0);
        lv_obj_center(result_label);
        lv_obj_set_style_text_color(result_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_color(result_label, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(result_label, LV_OPA_60, 0);
        lv_obj_set_style_pad_all(result_label, 20, 0);
        lv_obj_set_style_radius(result_label, 10, 0);
        lv_obj_add_flag(result_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(result_label, result_click_cb, LV_EVENT_CLICKED, NULL);
    }
}

static void start_game(void)
{
    score = 0;
    time_left = GAME_TIME;
    game_running = 1;

    char buf[32];
    snprintf(buf, sizeof(buf), "Score: %d", score);
    lv_label_set_text(score_label, buf);
    snprintf(buf, sizeof(buf), "Time: %ds", time_left);
    lv_label_set_text(time_label, buf);

    for (int r = 0; r < GRID_SIZE; r++)
        for (int c = 0; c < GRID_SIZE; c++)
            hide_mole(r, c);

    if (game_clock) { lv_timer_del(game_clock); game_clock = NULL; }
    if (mole_spawner) { lv_timer_del(mole_spawner); mole_spawner = NULL; }

    game_clock = lv_timer_create(game_tick, 1000, NULL);
    mole_spawner = lv_timer_create(spawn_mole, 600, NULL);

}

//点击结束按钮后重新初始化游戏
static void result_click_cb(lv_event_t *e)
{
    if (result_label) {
        lv_obj_del(result_label);
        result_label = NULL;
    }
    start_game();

    // score = 0;
    // time_left = GAME_TIME;
    // game_running = 1;

    // char buf[32];
    // snprintf(buf, sizeof(buf), "Score: %d", score);
    // lv_label_set_text(score_label, buf);
    // snprintf(buf, sizeof(buf), "Time: %ds", time_left);
    // lv_label_set_text(time_label, buf);

    // for (int r = 0; r < GRID_SIZE; r++)
    //     for (int c = 0; c < GRID_SIZE; c++)
    //         hide_mole(r, c);

    // if (game_clock) { lv_timer_del(game_clock); game_clock = NULL; }
    // if (mole_spawner) { lv_timer_del(mole_spawner); mole_spawner = NULL; }

    // game_clock = lv_timer_create(game_tick, 1000, NULL);
    // mole_spawner = lv_timer_create(spawn_mole, 600, NULL);
}

static void start_btn_cb(lv_event_t *e)
{
    if (start_btn) {
        lv_obj_del(start_btn);
        start_btn = NULL;
    }
    start_game();
}

void draw_whack_a_mole_on_screen(lv_obj_t *parent)
{
    container = parent;
    char path[256] = {0};

    srand((unsigned int)time(NULL));

    // 背景图片
    bg_bmp = lv_image_create(container);
    lv_image_set_src(bg_bmp, "A:/tapir/bmp/wrack_background.bmp");
    lv_obj_set_size(bg_bmp, 1024, 600);
    lv_obj_set_pos(bg_bmp, 0, 0);
    // lv_obj_set_style_bg_color(container, lv_color_hex(0x228B22), 0);

    // 得分标签
    score_label = lv_label_create(container);
    lv_label_set_text(score_label, "Score: 0");
    lv_obj_set_style_text_font(score_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(score_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(score_label, LV_ALIGN_TOP_LEFT, 10, 10);

    // 时间标签
    time_label = lv_label_create(container);
    lv_label_set_text(time_label, "Time: 30s");
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(time_label, LV_ALIGN_TOP_RIGHT, -10, 10);

    // 3*3的洞
    int grid_w = GRID_SIZE * (HOLE_R * 2 + 20) - 20;
    int start_x = (lv_obj_get_width(container) - grid_w) / 2;
    int start_y = 60;

    for (int r = 0; r < GRID_SIZE; r++) {
        for (int c = 0; c < GRID_SIZE; c++) {
            int x = start_x + c * (HOLE_R * 2 + 20);
            int y = start_y + r * (HOLE_R * 2 + 20);

            holes[r][c] = lv_btn_create(container);
            lv_obj_set_size(holes[r][c], HOLE_R * 2, HOLE_R * 2);
            lv_obj_set_pos(holes[r][c], x, y);
            lv_obj_set_style_radius(holes[r][c], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(holes[r][c], lv_color_hex(0x8B4513), 0);
            lv_obj_set_style_border_width(holes[r][c], 3, 0);
            lv_obj_set_style_border_color(holes[r][c], lv_color_hex(0x000000), 0);

            // hole_labels[r][c] = lv_label_create(holes[r][c]);
            // lv_label_set_text(hole_labels[r][c], "");
            // lv_obj_set_align(hole_labels[r][c], LV_ALIGN_CENTER);
            // lv_obj_set_style_text_font(hole_labels[r][c], &lv_font_montserrat_48, 0);
            // lv_obj_set_style_text_color(hole_labels[r][c], lv_color_hex(0xFFFFFF), 0);

            // lv_style_init(&bmp_style[r*3+c]);  
            bmp[r][c] = lv_image_create(holes[r][c]);
            lv_obj_set_align(bmp[r][c], LV_ALIGN_CENTER);
            sprintf(path, "%s", PIC_MOLE_PATH);
            lv_image_set_src(bmp[r][c], path);
            lv_obj_set_size(bmp[r][c],HOLE_R * 2, HOLE_R * 2);
            lv_obj_set_pos(bmp[r][c], 0, 0);
            lv_obj_add_flag(bmp[r][c], LV_OBJ_FLAG_HIDDEN);
            // lv_style_set_radius(&bmp_style[r*3+c], LV_RADIUS_CIRCLE);
            // lv_obj_add_style(bmp[r*3+c] ,&bmp_style[r*3+c], 0);



            active_hole[r][c] = 0;
            lv_obj_add_event_cb(holes[r][c], hole_click_cb, LV_EVENT_CLICKED, NULL);
        }
    }


    // 创建开始按钮（不自动开始）
    start_btn = lv_btn_create(container);
    lv_obj_set_size(start_btn, 200, 80);
    lv_obj_center(start_btn);
    lv_obj_t *start_label = lv_label_create(start_btn);
    lv_label_set_text(start_label, "Start");
    lv_obj_center(start_label);
    lv_obj_set_style_text_font(start_label, &lv_font_montserrat_48, 0);
    lv_obj_add_event_cb(start_btn, start_btn_cb, LV_EVENT_CLICKED, NULL);
    // 开始计时
    // score = 0;
    // time_left = GAME_TIME;
    // game_running = 1;

    // game_clock = lv_timer_create(game_tick, 1000, NULL);
    // mole_spawner = lv_timer_create(spawn_mole, 600, NULL);
}



