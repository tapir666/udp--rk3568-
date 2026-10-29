#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include "lvgl/src/widgets/canvas/lv_canvas.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include "main_src.h"
#include "mysrc/inc/game_2048.h"
#include "mysrc/inc/chinese_chess.h"
#include "mysrc/inc/whack_a_hole.h"
#include "mysrc/inc/video.h"
#include "mysrc/inc/linklist.h"


Node *play = NULL;

int main(void)
{
    lv_init();

    /*Linux frame buffer device init*/
    lv_display_t * disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, "/dev/fb0");
    lv_indev_t * indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/event6");
    
    //main_src_init();
    
    // //检索目录
    // Linklist *head = create_linklist();//创建一个空链表
    // save_path(head, "/tapir/video");//目录下满足为mp4 / .avi 路径保存在链表中
    // play = head->first;
    // mplayer_start(play->data);
    // mplayer_ui();

    video_playlist_init();    // 建链表 + 扫描 /tapir/video + 设 play
    main_src_init();

    /*Handle LVGL tasks*/
    while(1) {
        
        lv_timer_handler();
        usleep(5000);
    }

    return 0;
}
