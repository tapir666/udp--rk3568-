#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "chinese_chess.h"
#include "../lv_ls_font_24.h"
#include "../../lvgl/lvgl.h"
#include "../../lvgl/src/widgets/canvas/lv_canvas.h"


static lv_color_t canvas_buf[CW * CH];
static piece_t      pieces[TOTAL_PIECES];       // 32 个棋子
static int8_t       board[BOARD_COLS][BOARD_ROWS]; // -1 = 空，>=0 = pieces[] 索引
static game_phase_t phase = STATE_SELECTING;
static side_t       turn = SIDE_RED;
static int          selected_idx = -1; // 当前选中棋子的索引
static int          piece_count = 0;
static lv_obj_t *board_canvas = NULL;
static lv_obj_t *result_label = NULL;   // 结果标签
static lv_obj_t *undo_btn = NULL;
static int game_over = 0;
static lv_obj_t *chess_parent = NULL;
extern lv_obj_t *b_src;

static lv_obj_t *check_label = NULL;   

void draw_board_on_screen(lv_obj_t *parent)
{
    chess_parent = parent;
    board_init();
    draw_board();
}

// --- 悔棋（只存一步）
typedef struct {
    int piece_idx;
    int src_col, src_row;
    int dst_col, dst_row;
    int captured_idx;   // -1 = 没吃子
} move_record_t;

static move_record_t last_move;
static int has_last_move = 0;//只能悔一次

void add_piece(piece_type_t type,int col,int row,side_t side)
{
    pieces[piece_count].type = type;
    pieces[piece_count].side = side;
    pieces[piece_count].col = col;   //在第几行
    pieces[piece_count].row = row;   //在第几列
    pieces[piece_count].alive = 1;
    board[col][row] = piece_count++;  
}

void board_init(void)
{
    memset(board, -1, sizeof(board)); //棋盘上所在棋子的索引，无则为0

    //row 0  按照顺序放入车 马 象 士 将 士 象 马 车
    piece_type_t bank_rank[] = {                  
        PIECE_ROOK, PIECE_HORSE, PIECE_ELEPHANT, PIECE_ADVISOR, PIECE_KING,
        PIECE_ADVISOR, PIECE_ELEPHANT, PIECE_HORSE, PIECE_ROOK
    };

    for (int c = 0; c < 9; c++) {            
        add_piece(bank_rank[c], c, 0, SIDE_BLACK);  
    }

    // row 2 放炮
    add_piece(PIECE_CANNON, 1, 2, SIDE_BLACK);
    add_piece(PIECE_CANNON, 7, 2, SIDE_BLACK);

    // row 3 放兵
    for (int c = 0; c < 9; c += 2){
        add_piece(PIECE_PAWN, c, 3, SIDE_BLACK);
    }

    // 黑方放置完毕

    // row 4 放红方兵
    for (int c = 0; c < 9; c += 2){
        add_piece(PIECE_PAWN, c, 6, SIDE_RED);
    }

    // row 5 放红方炮
    add_piece(PIECE_CANNON, 1, 7, SIDE_RED);
    add_piece(PIECE_CANNON, 7, 7, SIDE_RED);

    // row 7 按照顺序放入红方车 马 象 士 将 士 象 马 车
    for (int c = 0; c < 9; c++) {            
        add_piece(bank_rank[c], c, 9, SIDE_RED);  
    }
}
const char *piece_char(piece_t *p)
{
    if (p->side == SIDE_RED) {
        switch (p->type) {
            case PIECE_KING:     return "帅";
            case PIECE_ADVISOR:  return "仕";
            case PIECE_ELEPHANT: return "相";
            case PIECE_HORSE:    return "马";
            case PIECE_ROOK:     return "车";
            case PIECE_CANNON:   return "炮";
            case PIECE_PAWN:     return "兵";
        }
    } else {
        switch (p->type) {
            case PIECE_KING:     return "将";
            case PIECE_ADVISOR:  return "士";
            case PIECE_ELEPHANT: return "象";
            case PIECE_HORSE:    return "马";
            case PIECE_ROOK:     return "车";
            case PIECE_CANNON:   return "炮";
            case PIECE_PAWN:     return "卒";
        }
    }
    return "?";
}

static lv_obj_t *piece_labels[TOTAL_PIECES];   //用来放置棋子文字标签
static lv_obj_t *piece_circles[TOTAL_PIECES];  //用来放置棋子ui

/*
void draw_piece(lv_layer_t *layer,int idx)
{
    piece_t *p = &pieces[idx];
    if (!p->alive) {
        // 如果棋子已被吃掉，隐藏对应的 label
        if (piece_labels[idx])
            lv_obj_add_flag(piece_labels[idx], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_draw_rect_dsc_t piece_dsc;
    lv_draw_rect_dsc_init(&piece_dsc);

    // 开启填充
    piece_dsc.bg_opa = LV_OPA_COVER;
    piece_dsc.bg_color = lv_color_hex(0xFFE4B5);
    // 开启边框
    piece_dsc.border_width = 3;          // 圆环厚度
    piece_dsc.border_color = lv_color_hex(0x000000);
    piece_dsc.border_opa = LV_OPA_COVER;
    // 圆形开关
    piece_dsc.radius = LV_RADIUS_CIRCLE;

    lv_area_t ring_area = {65,10,125,70}; // 直径60
    lv_draw_rect(layer, &piece_dsc, &ring_area);
}
*/

static lv_style_t piece_style;
void create_piece_ui(int idx)
{
    piece_t *p = &pieces[idx];
    int cx = PADW + p->row * CELL;
    int cy = PADH + p->col * CELL;
    int r  = CELL / 2 - 2;

    // ——— 圆形背景 ———
    piece_circles[idx] = lv_obj_create(board_canvas);//board_canvas
    lv_obj_set_size(piece_circles[idx], r * 2, r * 2);
    lv_obj_set_pos(piece_circles[idx],  cx - r, cy - r);  //CANVAS_X
    lv_obj_set_style_radius(piece_circles[idx], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(piece_circles[idx], 2, 0);
    lv_obj_set_style_border_color(piece_circles[idx], lv_color_hex(0x000000), 0);
    lv_style_set_pad_all(&piece_style, 0); //设置内边距为0
    lv_style_set_border_width(&piece_style, 0); //边框宽度为0

    // lv_obj_set_style_transform_angle(piece_circles[idx], 900, 0);
    // lv_obj_set_style_transform_pivot_x(piece_circles[idx], LV_PCT(50), 0);
    // lv_obj_set_style_transform_pivot_y(piece_circles[idx], LV_PCT(50), 0);

    lv_obj_add_style(piece_circles[idx] , &piece_style, 0);


    //旋转棋子，因为棋盘是横着放的
    if(p->side == SIDE_BLACK){
        lv_obj_set_style_transform_angle(piece_circles[idx], 900, 0);
        // 设置旋转中心点（控件中心旋转）
        lv_obj_set_style_transform_pivot_x(piece_circles[idx], r, 0);
        lv_obj_set_style_transform_pivot_y(piece_circles[idx], r, 0);
    }else if(p->side == SIDE_RED)
    {
        lv_obj_set_style_transform_angle(piece_circles[idx], -900, 0);
        // 设置旋转中心点（控件中心旋转）
        lv_obj_set_style_transform_pivot_x(piece_circles[idx], r, 0);
        lv_obj_set_style_transform_pivot_y(piece_circles[idx], r, 0);
    }

    lv_obj_add_flag(piece_circles[idx], LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    if (p->side == SIDE_RED) {
        lv_obj_set_style_bg_color(piece_circles[idx], lv_color_hex(0xFFE4B5), 0);   // 米色
        lv_obj_set_style_text_color(piece_circles[idx], lv_color_hex(0xCC0000), 0); // 红字
    } else {
        lv_obj_set_style_bg_color(piece_circles[idx], lv_color_hex(0x555555), 0);   // 深灰
        lv_obj_set_style_text_color(piece_circles[idx], lv_color_hex(0xFFFFFF), 0); // 白字
    }

    // ——— 文字 label，居中放在圆形上面 ———
    piece_labels[idx] = lv_label_create(piece_circles[idx]);

    lv_obj_remove_flag(piece_labels[idx], LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(piece_labels[idx], piece_char(p));
    lv_obj_set_style_text_align(piece_labels[idx], LV_TEXT_ALIGN_CENTER, 0);
    // lv_obj_set_size(piece_labels[idx], 36, 36);
    lv_obj_set_align(piece_labels[idx], LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(piece_labels[idx], &lv_ls_font_24, 0); 

            // lv_obj_set_style_transform_angle(piece_labels[idx], 900, 0);
            // // 设置旋转中心点（控件中心旋转）
            // lv_obj_set_style_transform_pivot_x(piece_labels[idx], r, 0);
            // lv_obj_set_style_transform_pivot_y(piece_labels[idx], r, 0);
            // lv_obj_add_flag(piece_labels[idx], LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    lv_obj_remove_flag(piece_circles[idx], LV_OBJ_FLAG_CLICKABLE);
    
}

void create_all_piece_ui()
{
    for(int i=0;i<TOTAL_PIECES;i++)
    {
        if(pieces[i].alive == 1)
        {
            create_piece_ui(i);
        }
    }
}

void highlight(int idx ,int on)
{
    piece_t *p = &pieces[idx];
    printf("highlight:(%d,%d)",p->col,p->row);
    if (on) {
        lv_obj_set_style_shadow_color(piece_circles[idx], lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_shadow_width(piece_circles[idx], 10, 0);
    } else {
        lv_obj_set_style_shadow_width(piece_circles[idx], 0, 0);
    }
}

void refresh_poi(int idx)
{
    piece_t *p = &pieces[idx];
    int cx = PADW + p->row * CELL;
    int cy = PADH + p->col * CELL;
    int r  = CELL / 2 - 2;

    lv_obj_set_pos(piece_circles[idx], cx - r, cy - r);
    printf("refresh:%d,(%d,%d)",idx,cx,cy);
    lv_obj_clear_flag(piece_circles[idx], LV_OBJ_FLAG_HIDDEN); // 如果之前被隐藏

    if (piece_labels[idx])
    {
        lv_obj_clear_flag(piece_labels[idx], LV_OBJ_FLAG_HIDDEN);
    }

}

void undo_cb(lv_event_t *e)
{
    if (!has_last_move || game_over) return;
    has_last_move = 0;

    // 清除将杀后的结果标签
    if (result_label != NULL) {
        lv_obj_del(result_label);
        result_label = NULL;
    }
    game_over = 0;

    if (last_move.captured_idx != -1) {
        pieces[last_move.captured_idx].alive = 1;
        pieces[last_move.captured_idx].col   = last_move.dst_col;
        pieces[last_move.captured_idx].row   = last_move.dst_row;
        board[last_move.dst_col][last_move.dst_row] = last_move.captured_idx;
        refresh_poi(last_move.captured_idx);
    } else {
        board[last_move.dst_col][last_move.dst_row] = -1;
    }

    board[last_move.src_col][last_move.src_row] = last_move.piece_idx;
    pieces[last_move.piece_idx].col = last_move.src_col;
    pieces[last_move.piece_idx].row = last_move.src_row;
    refresh_poi(last_move.piece_idx);

    // 换回上一步的玩家
    turn = (turn == SIDE_RED) ? SIDE_BLACK : SIDE_RED;

    if (selected_idx != -1) {
        highlight(selected_idx, 0);
        selected_idx = -1;
    }
    phase = STATE_SELECTING;
}

void do_move(int idx,int tr_x,int tr_y)
{
    piece_t *p = &pieces[idx];
    int src_x = p->col;
    int src_y = p->row;
    printf("SRCx=%d SRCy=%d\n", src_x, src_y);
    printf("TRx=%d TRy=%d\n", tr_x, tr_y);
    int target = board[tr_x][tr_y];
    last_move = (move_record_t){idx, src_x, src_y, tr_x, tr_y, target};
    has_last_move = 1;
    if(target != -1)
    {
        pieces[target].alive = 0;
        lv_obj_add_flag(piece_circles[target], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(piece_labels[target], LV_OBJ_FLAG_HIDDEN);
    }
    board[src_x][src_y] = -1;
    board[tr_x][tr_y] = idx;
    p->col = tr_x;
    p->row = tr_y;
    refresh_poi(idx);
}
//判断目的棋盘处是否为友军
int is_friendly(int tr_x,int tr_y)
{
    int idx = board[tr_x][tr_y];
    if(idx >= 0 && pieces[idx].side == turn)return 1;
    return 0;
}
//判断目的棋盘处是否为敌军
int is_enemy(int tr_x,int tr_y)
{
    int idx = board[tr_x][tr_y];
    if(idx >= 0 && pieces[idx].side != turn)return 1;
    return 0;
}

int in_board(int tr_x, int tr_y)
{
    return tr_x >= 0 && tr_x < BOARD_COLS && tr_y >= 0 && tr_y < BOARD_ROWS;
}

//——————————— 各棋子移动规则   ——————————————
/*
    返回值   1 可走
             0 不可走
*/

int king_can_move(piece_t *p ,int tr_x,int tr_y)
{
    int sr_x = p->col,sr_y = p->row;
    if(p->side == SIDE_BLACK)
    {
        if(tr_x < 3 || tr_x > 5 || tr_y > 3 || tr_y < 0)
        return 0;
    }
    if(p->side == SIDE_RED)
    {
        if(tr_x < 3 || tr_x > 5 || tr_y > 9 || tr_y < 7)
        return 0;
    }
    if(abs(sr_x - tr_x) + abs(sr_y - tr_y) != 1)return 0;

    return !is_friendly(tr_x,tr_y);
}

int advisor_can_move(piece_t *p ,int tr_x,int tr_y)
{
    int sr_x = p->col,sr_y = p->row;
    if(p->side == SIDE_BLACK)
    {
        if(tr_x < 3 || tr_x > 5 || tr_y > 3 || tr_y < 0)
        return 0;
    }
    if(p->side == SIDE_RED)
    {
        if(tr_x < 3 || tr_x > 5 || tr_y > 9 || tr_y < 7)
        return 0;
    }
    if(abs(sr_x - tr_x) != 1 || abs(sr_y - tr_y) != 1)return 0;

    return !is_friendly(tr_x,tr_y);
}

//象不过河并且不能被堵象眼
int elephant_can_move(piece_t *p ,int tr_x,int tr_y)
{
    int sr_x = p->col,sr_y = p->row;
    if(p->side == SIDE_BLACK)
    {
        if(tr_y > 4)
        return 0;
    }
    if(p->side == SIDE_RED)
    {
        if(tr_y < 5)
        return 0;
    }
    if(abs(sr_x - tr_x) != 2 || abs(sr_y - tr_y) != 2)return 0;
    if(board[(sr_x+tr_x)/2][(sr_y+tr_y)/2] != -1)return 0;

    return !is_friendly(tr_x,tr_y);
}

//马走日字形并且不能别马脚
int horse_can_move(piece_t *p ,int tr_x,int tr_y)
{
    int sr_x = p->col,sr_y = p->row;
    int dif_x = tr_x - sr_x,dif_y = tr_y - sr_y;
    if(((abs(dif_x)==2 && abs(dif_y)==1)||(abs(dif_x)==1 && abs(dif_y)==2)) != 1)return 0;
    if(abs(dif_x)==2 && abs(dif_y)==1)
    {
        if(board[dif_x/2+sr_x][sr_y] != -1)return 0;
    }else
    {
        if(board[sr_x][dif_y/2+sr_y] != -1)return 0;
    }

    return !is_friendly(tr_x,tr_y);
}

//车走直线
int rook_can_move(piece_t *p ,int tr_x,int tr_y)
{
    int sr_x = p->col,sr_y = p->row;
    if(sr_x!=tr_x && sr_y!=tr_y)return 0;
    if(sr_x == tr_x)
    {
        int min = sr_y < tr_y ? sr_y : tr_y;
        int max = sr_y > tr_y ? sr_y : tr_y;
        for(int i=min+1;i<max;i++)
        {
            if(board[sr_x][i] != -1)return 0;
        }
    }else
    {
        int min = sr_x < tr_x ? sr_x : tr_x;
        int max = sr_x > tr_x ? sr_x : tr_x;
        for(int i=min+1;i<max;i++)
        {
            if(board[i][sr_y] != -1)return 0;
        }
    }
    return !is_friendly(tr_x, tr_y);
}

int cannon_can_move(piece_t *p ,int tr_x,int tr_y)
{
    int sr_x = p->col,sr_y = p->row;
    if(sr_x!=tr_x && sr_y!=tr_y)return 0;
    int count = 0;
    if (sr_x == tr_x) {
        // 同一列，水平方向移动
        int min = sr_y < tr_y ? sr_y : tr_y;
        int max = sr_y > tr_y ? sr_y : tr_y;
        for (int i = min + 1; i < max; i++) {
            if (board[sr_x][i] != -1) count++;  
        }
    } else {
        // 同一行，垂直方向移动
        int min = sr_x < tr_x ? sr_x : tr_x;
        int max = sr_x > tr_x ? sr_x : tr_x;
        for (int i = min + 1; i < max; i++) {
            if (board[i][sr_y] != -1) count++;   
        }
    }

    if (board[tr_x][tr_y] == -1)                  // 落空位
        return (count == 0);
    else if (is_enemy(tr_x, tr_y))                // 吃子
        return (count == 1);
    return 0;
}

int pawn_can_move(piece_t *p ,int tr_x,int tr_y)
{
    int sr_x = p->col,sr_y = p->row;
    int dif_x = tr_x - sr_x,dif_y = tr_y - sr_y;
    if(abs(dif_x)+abs(dif_y) != 1)return 0;
    if(p->side == SIDE_BLACK)
    {
        if(sr_y < 5)
        {
            if(dif_y != 1)return 0;
        }else
        {
            if(dif_y == -1)return 0;
        }
    }
    if(p->side == SIDE_RED)
    {
        if(sr_y > 4)
        {
            if(dif_y != -1)return 0;
        }else
        {
            if(dif_y == 1)return 0;
        }        
    }

    return !is_friendly(tr_x,tr_y);
}

//——————————————————————————————————————————

//判断移动是否合乎规则
int is_valid_move(piece_t *p,int col,int row)
{
    if(in_board(col,row)!=1)return 0;
    switch (p->type) {
            case PIECE_KING:     return king_can_move(p,col,row);
            case PIECE_ADVISOR:  return advisor_can_move(p,col,row);
            case PIECE_ELEPHANT: return elephant_can_move(p,col,row);
            case PIECE_HORSE:    return horse_can_move(p,col,row);
            case PIECE_ROOK:     return rook_can_move(p,col,row);
            case PIECE_CANNON:   return cannon_can_move(p,col,row);
            case PIECE_PAWN:     return pawn_can_move(p,col,row);
    }
    return 0;
}

//找到当前帅的位置
int find_king(side_t side)
{
    int idx = 0;
    for(int i=0;i<TOTAL_PIECES;i++)
    {
        if(pieces[i].type == PIECE_KING && pieces[i].side == side && pieces[i].alive ==1)
        {
            return i;
        }
    }
    return -1;
}

//判断是否将军
int check_king(side_t side)
{
    int idx = find_king(side);
    if(idx < 0)
    {
        return -1;   
    }
    piece_t *king = &pieces[idx];
    side_t enemy = (side == SIDE_RED) ? SIDE_BLACK : SIDE_RED;
    for(int i=0;i<TOTAL_PIECES;i++)
    {
        if(pieces[i].alive == 1 && pieces[i].side == enemy )
        {
            if(is_valid_move(&pieces[i],king->col,king->row))return 1;
        }
    }

    //判断对将情况
    int enemy_idx = find_king(enemy);
    piece_t *enemy_king = &pieces[enemy_idx];
    if(enemy_king->col == king->col)
    {
        int min = enemy_king->row > king->row ? king->row : enemy_king->row;
        int max = enemy_king->row > king->row ? enemy_king->row : king->row;
        for(int i=min+1;i<max;i++)
        {
            if(board[king->col][i] != -1)
            {
                return 0;
            }
        }
        return 1;
    }
    return 0;
}

//判断游戏是否结束，当前行走方的帅被吃掉则结束
int check_end(side_t side)
{
    for(int i=0;i<TOTAL_PIECES;i++)
    {
        if(pieces[i].type == PIECE_KING && pieces[i].side == side)
        {
            return (pieces[i].alive != 1);
        }
    }
}

void chinese_chess_restart(lv_event_t * e)
{
    if(check_label != NULL)   
    { 
        lv_obj_del(check_label);   
        check_label = NULL;   
    }
    if(board_canvas != NULL)
    {
        lv_obj_del(board_canvas);   // 会连带删除所有子对象（棋子 circle 和 label）
        board_canvas = NULL;
    }
    if(result_label != NULL)
    {
        lv_obj_del(result_label);
        result_label = NULL;
    }

    phase = STATE_SELECTING;
    turn = SIDE_RED;
    selected_idx = -1; // 当前选中棋子的索引
    piece_count = 0; 
    has_last_move = 0;
    game_over = 0;
    // board_init();
    draw_board_on_screen(b_src);
}

void show_chess_result_label(char *text)
{
    if(result_label != NULL) lv_obj_del(result_label);

    result_label = lv_label_create(chess_parent);
    lv_label_set_text(result_label, text);
    lv_obj_set_style_text_font(result_label, &lv_font_montserrat_48, 0);
    lv_obj_center(result_label);
    lv_obj_set_style_text_color(result_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(result_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(result_label, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(result_label, 20, 0);
    lv_obj_set_style_radius(result_label, 10, 0);

    // 可点击，绑定重启回调
    lv_obj_add_flag(result_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(result_label, chinese_chess_restart, LV_EVENT_CLICKED, NULL);    
}

// 显示"将军！"提示（1.5 秒后自动消失）
static void check_label_timer_cb(lv_timer_t *timer)
{
    lv_timer_del(timer);
    if (check_label != NULL) {
        lv_obj_del(check_label);
        check_label = NULL;
    }
}

static void show_check_label(const char *text)
{
    if (check_label != NULL) {
        lv_obj_del(check_label);
        check_label = NULL;
    }

    check_label = lv_label_create(chess_parent);
    lv_label_set_text(check_label, text);
    lv_obj_set_style_text_font(check_label, &lv_ls_font_24, 0);
    lv_obj_set_style_text_color(check_label, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_color(check_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(check_label, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(check_label, 6, 0);
    lv_obj_set_style_radius(check_label, 6, 0);
    lv_obj_align(check_label, LV_ALIGN_CENTER, 0, -CH/2 + 16);

    lv_timer_create(check_label_timer_cb, 1500, NULL);
}

//点击后回调函数
void board_click_cb(lv_event_t *e)
{
    if (game_over) return;
    lv_point_t click_point;
    //获取输入设备指针
    lv_indev_t *input_device = lv_indev_active(); 
    lv_indev_get_point(lv_indev_get_act(), &click_point);
    //获取点击屏幕坐标
    printf("x=%d y=%d\n", click_point.x, click_point.y);
    click_point.x -= CANVAS_X;
    printf("x=%d y=%d\n", click_point.x, click_point.y);
    int rowy = (click_point.x - PADW + CELL/2) / CELL;     //在第几列
    int colx = (click_point.y - PADH + CELL/2) / CELL;     //在第几行
    printf("x(rowy)=%d y(colx)=%d\n", colx, rowy);
    if (rowy < 0 || rowy >= BOARD_ROWS || colx < 0 || colx >= BOARD_COLS)
    return;
    int idx = board[colx][rowy];
    if (phase == STATE_SELECTING) {
        // 未选择
        if (idx != -1 && pieces[idx].alive && pieces[idx].side == turn) {
            selected_idx = idx;
            highlight(idx, 1);  // 高亮
            phase = STATE_MOVING;
        }
    }
    else if(phase == STATE_MOVING)
    {   //已选择，准备移动
        if(idx != -1 && pieces[idx].alive && pieces[idx].side == turn) //切换选择棋子，只有当选的不是空，以及选的是当前方活着的棋子
        {
            highlight(selected_idx ,0);
            selected_idx = idx;
            highlight(selected_idx ,1);
        }else if(is_valid_move(&pieces[selected_idx], colx, rowy))
        {
            //若为有效移动，执行移动之后，取消高亮，更换选棋状态，更换选棋方
            do_move(selected_idx, colx, rowy);
            highlight(selected_idx ,0);
            selected_idx = -1;
            phase = STATE_SELECTING;
            side_t opponent = (turn == SIDE_RED) ? SIDE_BLACK : SIDE_RED;
            if(check_end(opponent))
            {
                game_over = 1;
                if(opponent == SIDE_BLACK)
                {
                    show_chess_result_label("Red Win!\nTap to restart");
                }
                if(opponent == SIDE_RED)
                {
                    show_chess_result_label("Black Win!\nTap to restart");
                }
                
            }
            if(check_king(opponent)==1)
            {
                switch(opponent)
                {
                    case SIDE_BLACK:
                        printf("红方将军00000\n"); 
                        show_check_label("红方将军");
                        break;
                    case SIDE_RED:  
                        printf("黑方将军11111\n");
                        show_check_label("黑方将军"); 
                        break; 
                    default: 
                        break;
                }
                
            }
            turn = opponent;
        }else if(!is_valid_move(&pieces[selected_idx], colx, rowy))
        {
            //无效移动->取消高亮，取消选择
            highlight(selected_idx ,0);
            selected_idx = -1;
            phase = STATE_SELECTING;
        }
    } 
    
}

void draw_board()
{

    lv_style_init(&piece_style);
    lv_style_set_pad_all(&piece_style, 0);
    lv_style_set_border_width(&piece_style, 0);
    // 预设置阴影默认属性，动态修改才生效
    lv_style_set_shadow_width(&piece_style, 0);
    lv_style_set_shadow_color(&piece_style, lv_color_black());
    lv_style_set_shadow_opa(&piece_style, LV_OPA_COVER);
    //------------棋盘网格初始化
    board_canvas = lv_canvas_create(chess_parent); 
    lv_canvas_set_buffer(board_canvas, canvas_buf, CW, CH, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_align(board_canvas, LV_ALIGN_CENTER);
    lv_obj_set_size(board_canvas,CW,CH);
    lv_canvas_fill_bg(board_canvas, lv_color_hex(0xF08650), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(board_canvas, &layer);
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_hex(0x000000);
    dsc.width = 5;
    //画9条横线
    for (int r = 0; r < BOARD_COLS; r++){
        if(r == 0 || r == BOARD_COLS - 1)
        {
            dsc.p1 = (lv_point_precise_t){PADW, PADH + r * CELL};
            dsc.p2 = (lv_point_precise_t){PADW + (BOARD_ROWS-1) * CELL, PADH + r * CELL};
            lv_draw_line(&layer,&dsc);
        }else{
            dsc.p1 = (lv_point_precise_t){PADW, PADH + r * CELL};
            dsc.p2 = (lv_point_precise_t){PADW + (BOARD_ROWS/2-1) * CELL, PADH + r * CELL};
            lv_draw_line(&layer,&dsc);
            dsc.p1 = (lv_point_precise_t){PADW + (BOARD_ROWS/2) * CELL, PADH + r * CELL};
            dsc.p2 = (lv_point_precise_t){PADW + (BOARD_ROWS-1) * CELL, PADH + r * CELL};
            lv_draw_line(&layer,&dsc);
        }
    }
    //画10条竖线
    for (int r = 0; r < BOARD_ROWS; r++){
        dsc.p1 = (lv_point_precise_t){PADW + r * CELL, PADH};
        dsc.p2 = (lv_point_precise_t){PADW + r * CELL, PADH + (BOARD_COLS-1) * CELL};
        lv_draw_line(&layer,&dsc);
    }
    //画士所能走的斜线
    dsc.p1 = (lv_point_precise_t){PADW, PADH + 3 * CELL};
    dsc.p2 = (lv_point_precise_t){PADW + 2 * CELL, PADH + 5 * CELL};
    lv_draw_line(&layer,&dsc);
    dsc.p1 = (lv_point_precise_t){PADW, PADH + 5 * CELL};
    dsc.p2 = (lv_point_precise_t){PADW + 2 * CELL, PADH + 3 * CELL};
    lv_draw_line(&layer,&dsc);
    dsc.p1 = (lv_point_precise_t){PADW + 7 * CELL, PADH + 3 * CELL};
    dsc.p2 = (lv_point_precise_t){PADW + 9 * CELL, PADH + 5 * CELL};
    lv_draw_line(&layer,&dsc);
    dsc.p1 = (lv_point_precise_t){PADW + 7 * CELL, PADH + 5 * CELL};
    dsc.p2 = (lv_point_precise_t){PADW + 9 * CELL, PADH + 3 * CELL};
    lv_draw_line(&layer,&dsc);

    //-------------结束棋盘初始化
    lv_canvas_finish_layer(board_canvas, &layer);
    
    // --------------棋子初始化
    create_all_piece_ui();

    lv_obj_add_flag(board_canvas, LV_OBJ_FLAG_CLICKABLE);
        // 悔棋按钮
    undo_btn = lv_btn_create(chess_parent);
    lv_obj_set_size(undo_btn, 70, 40);
    lv_obj_align(undo_btn, LV_ALIGN_CENTER, -(CW/2 + 70), 0);

        // lv_obj_set_style_transform_angle(undo_btn, 900, 0);
        // // 设置旋转中心点（控件中心旋转）
        // lv_obj_set_style_transform_pivot_x(undo_btn, LV_PCT(50), 0);
        // lv_obj_set_style_transform_pivot_y(undo_btn, LV_PCT(50), 0);

    lv_obj_t *undo_label = lv_label_create(undo_btn);
    lv_label_set_text(undo_label, "悔棋");
    lv_obj_center(undo_label);
    lv_obj_set_style_text_font(undo_label, &lv_ls_font_24, 0);
    lv_obj_add_event_cb(undo_btn, undo_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(board_canvas, board_click_cb, LV_EVENT_CLICKED, NULL);



} 