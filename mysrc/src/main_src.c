#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include "lvgl/src/widgets/canvas/lv_canvas.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include "mysrc/inc/game_2048.h"
#include "mysrc/inc/chinese_chess.h"
#include "mysrc/inc/whack_a_hole.h"
#include "main_src.h"
#include "mysrc/inc/sanguosha.h"
#include "mysrc/inc/file_transfer.h"
#include "mysrc/inc/video.h"
#include "mysrc/inc/remote_cmd.h"



extern lv_font_t lv_ls_font_24;
lv_obj_t *main_src;



static const char* SLOT_LABEL[6] = {
    "gif",           "Chinese chess", "2048",
    "To be developed","To be developed","To be developed",
};
static lv_obj_t *main_bg;

static slot_t slots[6];
extern lv_obj_t *san_exit_btn;
extern void draw_sanguosha_on_screen(lv_obj_t *parent);
extern void draw_file_transfer_on_screen(lv_obj_t *parent);

//----- A界面----
lv_obj_t *a_src;
lv_obj_t *a_gif;
lv_obj_t *a_exit_button;

//----- B界面----
lv_obj_t *b_src;
lv_obj_t *b_exit_button;

//----- C界面----
lv_obj_t *c_src;
lv_obj_t *c_exit_button;

//----- D界面----
lv_obj_t *d_src;
lv_obj_t *d_gif;
lv_obj_t *d_exit_button;

//----- E界面----
lv_obj_t *e_src;
lv_obj_t *e_gif;
lv_obj_t *e_exit_button;

//----- F界面----
lv_obj_t *f_src;
lv_obj_t *f_gif;
lv_obj_t *f_exit_button;


void main_interface_button_a_cb(lv_event_t *e)
{
    //获取事件编码
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED)
    {
        lv_screen_load(a_src);
    }
}

void main_interface_button_b_cb(lv_event_t *e)
{
    //获取事件编码
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED)
    {
        lv_screen_load(b_src);
    }
}

void main_interface_button_c_cb(lv_event_t *e)
{
    //获取事件编码
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED)
    {
        lv_screen_load(c_src);
    }
}

void main_interface_button_d_cb(lv_event_t *e)
{
    //获取事件编码
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED)
    {
        lv_screen_load(d_src);
    }
}

void main_interface_button_e_cb(lv_event_t *e)
{
    //获取事件编码
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED)
    {
        lv_screen_load(e_src);
    }
}

void main_interface_button_f_cb(lv_event_t *e)
{
    //获取事件编码
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED)
    {
        mplayer_play_if_idle();     
        lv_screen_load(f_src);
    }
}

void exit_button_cb(lv_event_t *e)
{
    //获取事件编码
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED)
    {
        if(main_src != NULL)
            lv_screen_load(main_src);
    }
}

void init_a_interface()
{
    //创建屏幕 A
    a_src = lv_obj_create(NULL); 
    //在屏幕A上创建一个动图控件
    // a_gif = lv_gif_create(a_src);
    // lv_obj_set_size(a_gif, 1024, 600);
    // lv_obj_set_align(a_gif, LV_ALIGN_LEFT_MID);
    // //在图片控件上显示一张动图
    // lv_gif_set_src(a_gif, "A:/tapir/1.gif");
    draw_whack_a_mole_on_screen(a_src);
    //创建按钮
    a_exit_button = lv_button_create(a_src);
    lv_obj_set_size(a_exit_button, 70, 40);
    lv_obj_set_align(a_exit_button, LV_ALIGN_TOP_RIGHT);
    lv_obj_add_event_cb(a_exit_button, exit_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *a_exit_label = lv_label_create(a_exit_button);
    lv_label_set_text(a_exit_label, "Exit");
    lv_obj_center(a_exit_label);
}


void init_b_interface()
{
    b_src = lv_obj_create(NULL);

    draw_board_on_screen(b_src);

    // Exit 按钮
    b_exit_button = lv_button_create(b_src);
    lv_obj_set_size(b_exit_button, 70, 40);
    lv_obj_set_align(b_exit_button, LV_ALIGN_TOP_RIGHT);
    lv_obj_add_event_cb(b_exit_button, exit_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *b_exit_label = lv_label_create(b_exit_button);
    lv_label_set_text(b_exit_label, "Exit");
    lv_obj_center(b_exit_label);
}

void init_c_interface()
{
    c_src = lv_obj_create(NULL);
    srand(time(NULL));

    draw_2048_on_screen(c_src);

    // Exit 按钮
    c_exit_button = lv_button_create(c_src);
    lv_obj_set_size(c_exit_button, 70, 40);
    lv_obj_set_align(c_exit_button, LV_ALIGN_TOP_RIGHT);
    lv_obj_add_event_cb(c_exit_button, exit_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *c_exit_label = lv_label_create(c_exit_button);
    lv_label_set_text(c_exit_label, "Exit");
    lv_obj_center(c_exit_label);
}

void init_d_interface()
{
    //创建屏幕 D
    d_src = lv_obj_create(NULL); 
    draw_sanguosha_on_screen(d_src);


    // Exit 按钮
    d_exit_button = lv_button_create(d_src);
    san_exit_btn = d_exit_button;
    lv_obj_set_size(d_exit_button, 70, 40);
    lv_obj_set_align(d_exit_button, LV_ALIGN_TOP_RIGHT);
    lv_obj_add_event_cb(d_exit_button, exit_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *d_exit_label = lv_label_create(d_exit_button);
    lv_label_set_text(d_exit_label, "Exit");
    lv_obj_center(d_exit_label);
}

void init_e_interface()
{
    //创建屏幕 E
    e_src = lv_obj_create(NULL); 
    // //在屏幕E上创建一个动图控件
    // e_gif = lv_image_create(e_src);
    // lv_obj_set_size(e_gif, 1024, 600);
    // lv_obj_set_align(e_gif, LV_ALIGN_CENTER);
    // //在图片控件上显示一张动图
    // lv_image_set_src(e_gif, "A:/tapir/e.bmp");

    draw_file_transfer_on_screen(e_src);

    // Exit 按钮
    e_exit_button = lv_button_create(e_src);
    lv_obj_set_size(e_exit_button, 70, 40);
    lv_obj_set_align(e_exit_button, LV_ALIGN_TOP_RIGHT);
    lv_obj_add_event_cb(e_exit_button, exit_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *e_exit_label = lv_label_create(e_exit_button);
    lv_label_set_text(e_exit_label, "Exit");
    lv_obj_center(e_exit_label);
}

void f_exit_button_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        mplayer_stop();
        lv_screen_load(main_src);
    }
}

void init_f_interface()
{
    //创建屏幕 F
    f_src = lv_obj_create(NULL); 
    mplayer_ui(f_src);

    f_exit_button = lv_button_create(f_src);
    lv_obj_set_size(f_exit_button, 50, 50);
    lv_obj_set_pos(f_exit_button, 224,500);
    lv_obj_add_event_cb(f_exit_button, f_exit_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *f_exit_label = lv_label_create(f_exit_button);
    lv_label_set_text(f_exit_label, "Exit");
    lv_obj_center(f_exit_label);
}

#define PIC_PATH "A:/tapir/bmp/bg"

lv_obj_t *bg[6];
void main_src_init(void)
{   
    char path[256] = {0};
    main_src = lv_screen_active();
    /* 背景图 */
    main_bg = lv_image_create(main_src);
    lv_image_set_src(main_bg, "A:/tapir/bmp/background.bmp");
    lv_obj_set_size(main_bg, 1024, 600);
    lv_obj_set_pos(main_bg, 0, 0);

    init_a_interface();
    init_b_interface();
    init_c_interface();
    init_d_interface();
    init_e_interface();
    init_f_interface();

    //启动远程命令服务：Linux主机通过UDP控制板子的游戏和播放器
    remote_cmd_init(8000);
    lv_timer_create(remote_cmd_timer_cb, 100, NULL);

    /* 3×2 grid: 200×200 slots, 40px gap, centered */
    // const int slot_w = 200, slot_h = 200, gap = 30;
    const int slot_w = 190, slot_h = 176, gap = 40;

    lv_coord_t start_x = (1024 - (3 * slot_w + 2 * gap)) / 2;  
    lv_coord_t start_y = (600 - (2 * slot_h + 1 * gap)) / 2;   

    for (int i = 0; i < 6; i++)
    {
        slot_t *s = &slots[i];
        int row = i / 3;
        int col = i % 3;

        s->obj = lv_obj_create(main_src);
        lv_obj_set_size(s->obj, slot_w, slot_h);
        lv_obj_set_pos(s->obj, start_x + col * (slot_w + gap) + 18, start_y + row * (slot_h + gap) + 3);


        //创建窗口背景图  
        lv_style_init(&s->bmp_style);             
        s->bmp = lv_image_create(s->obj);
        sprintf(path, "%s%d.bmp", PIC_PATH, i);
        lv_image_set_src(s->bmp, path);
        lv_obj_set_size(s->bmp, 190, 176);
        lv_obj_set_pos(s->bmp, 0, 0);
        lv_style_set_radius(&s->bmp_style, 20);
        lv_obj_add_style(s->bmp, &s->bmp_style, 0);

        lv_style_init(&s->bg_style);
        lv_style_set_bg_color(&s->bg_style, lv_color_hex(0x99B9CE));
        //lv_style_set_border_color(&s->bg_style, lv_color_hex(0x000000));    
        lv_style_set_pad_all(&s->bg_style, 0);
        lv_style_set_border_width(&s->bg_style, 0);
        lv_style_set_radius(&s->bg_style, 20);
        // lv_style_set_opa(&s->bg_style, LV_OPA_60); 
        lv_obj_add_style(s->obj, &s->bg_style, 0);

        s->btn = lv_button_create(s->obj);
        lv_obj_set_size(s->btn, slot_w, slot_h);
        lv_obj_set_align(s->btn, LV_ALIGN_CENTER);
        lv_style_init(&s->btn_style);
        lv_style_set_bg_color(&s->btn_style, lv_color_hex(0x99B9CE));
        lv_style_set_border_color(&s->btn_style, lv_color_hex(0x000000));
        lv_style_set_radius(&s->btn_style, 20);
        lv_style_set_opa(&s->btn_style, LV_OPA_TRANSP); 
        lv_obj_add_style(s->btn, &s->btn_style, 0);

        // s->label = lv_label_create(s->obj);
        // lv_label_set_text(s->label, SLOT_LABEL[i]);
        // lv_obj_set_align(s->label, LV_ALIGN_CENTER);
        // lv_style_init(&s->lab_style); 
        // lv_style_set_text_color(&s->lab_style, lv_color_hex(0x000000));
        // const lv_font_t *font_size = &lv_font_montserrat_20;
        // lv_style_set_text_font(&s->lab_style, font_size);
        // lv_obj_add_style(s->label, &s->lab_style, 0);

        s->evt.style      = &s->btn_style;
        s->evt.orig_color = lv_color_hex(0x00FF00);
    }

    lv_obj_add_event_cb(slots[0].btn, main_interface_button_a_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(slots[1].btn, main_interface_button_b_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(slots[2].btn, main_interface_button_c_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(slots[3].btn, main_interface_button_d_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(slots[4].btn, main_interface_button_e_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(slots[5].btn, main_interface_button_f_cb, LV_EVENT_CLICKED, NULL);
    lv_screen_load(main_src);
}
