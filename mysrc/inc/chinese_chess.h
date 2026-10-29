#ifndef __CHINESE_CHESS_H__
#define __CHINESE_CHESS_H__

#include "../../lvgl/lvgl.h"

#define BOARD_COLS  9         //9行
#define BOARD_ROWS  10        //10列
#define TOTAL_PIECES 32       //一共有32个棋子
#define CW 775                //棋盘宽
#define CH 600                //棋盘高
#define PADH 40               //高起始边距
#define PADW 95               //宽起始边距 
#define CANVAS_X 125          //棋盘相对屏幕偏移 
#define CELL 65               //每格的边距      
#define PIECE_SIZE 40         //棋子的大小       

// ——— 棋子类型 ———
typedef enum {
    PIECE_KING,      // 将/帅
    PIECE_ADVISOR,   // 士/仕
    PIECE_ELEPHANT,  // 象/相
    PIECE_HORSE,     // 马
    PIECE_ROOK,      // 车
    PIECE_CANNON,    // 炮
    PIECE_PAWN,      // 兵/卒
} piece_type_t;

// ——— 阵营 ———
typedef enum {
    SIDE_RED,
    SIDE_BLACK,
} side_t;

// ——— 一个棋子 ———
typedef struct {
    piece_type_t type;
    side_t       side;
    int          col;        // 在第col行
    int          row;        // 在第row列
    int      alive;      // 1 = 还在棋盘上
} piece_t;

// ——— 游戏阶段 ———


typedef enum {
    STATE_SELECTING,    // 等待选棋
    STATE_MOVING,       // 已选中，等待落子
} game_phase_t;

void board_init(void);
void draw_board_on_screen(lv_obj_t *parent);
void draw_board(void);



#endif