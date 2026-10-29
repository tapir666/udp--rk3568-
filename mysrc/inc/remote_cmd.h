#ifndef REMOTE_CMD_H
#define REMOTE_CMD_H

#include "../../lvgl/lvgl.h"

/**
 * remote_cmd_init: 启动远程命令服务（后台线程监听UDP端口）
 * @port: 监听的端口号
 * 返回值:
 *      成功 0
 *      失败 -1
 */
int remote_cmd_init(int port);

/**
 * remote_cmd_timer_cb: LVGL主循环定时器回调
 * 从命令队列取出远程命令，在LVGL主线程执行UI操作
 */
void remote_cmd_timer_cb(lv_timer_t *timer);

#endif
