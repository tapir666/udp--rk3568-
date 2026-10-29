#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include "sanguosha.h"
#include "../lv_sanguo_font_24.h"
#include "../../lvgl/lvgl.h"

/* 素材目录：图片统一放在 A:/tapir/sanguo/ */
#define SAN_IMG_DIR "A:/tapir/sanguo/"
#define CARD_W 142
#define CARD_H 200
#define AI_CARD_GAP 90
#define PLAYER_CARD_GAP 44
#define CHAR_CARD_GAP 24

/* ============================
 *   三国杀 单挑模式 (1v1 vs AI)
 *   简化版实现
 * ============================ */

/* ============ 卡片类型 ============ */
typedef enum {
    CARD_NONE = 0,
    CARD_KILL,      // 杀
    CARD_DODGE,     // 闪
    CARD_PEACH,     // 桃
    CARD_WUZHONG,   // 无中生有
    CARD_DISMANTLE, // 过河拆桥
    CARD_STEAL,     // 顺手牵羊
    CARD_BARBARIAN, // 南蛮入侵
    CARD_VOLLEY,    // 万箭齐发
    CARD_PEACE,     // 桃园结义
    CARD_DUEL,      // 决斗
    CARD_BAGUA,     // 八卦阵
    CARD_HAPPINESS, // 乐不思蜀
    CARD_THUNDER,   // 闪电
    CARD_TYPE_COUNT
} card_type_t;

typedef enum {
    SUIT_SPADE,   // 黑桃
    SUIT_HEART,   // 红桃
    SUIT_CLUB,    // 梅花
    SUIT_DIAMOND  // 方片
} card_suit_t;

typedef struct {
    card_type_t type;
    card_suit_t suit;
    int number;
} card_t;

/* ============ 武将 ============ */
typedef enum {
    CHAR_GUAN_YU,   // 关羽 - 武圣
    CHAR_ZHANG_FEI, // 张飞 - 咆哮
    CHAR_ZHAO_YUN,  // 赵云 - 龙胆
    CHAR_LV_BU,     // 吕布 - 无双
    CHAR_ZHOU_YU,   // 周瑜 - 英姿
    CHAR_HUA_TUO,   // 华佗 - 青囊
    CHAR_COUNT
} char_type_t;

typedef struct {
    char_type_t type;
    const char *name;
    const char *skill_name;
    const char *skill_desc;
    int max_hp;
} char_def_t;

static const char_def_t char_defs[CHAR_COUNT] = {
    {CHAR_GUAN_YU,   "关羽",  "武圣", "红色牌当杀",    4},
    {CHAR_ZHANG_FEI, "张飞",  "咆哮", "回合内可出多张杀", 4},
    {CHAR_ZHAO_YUN,  "赵云",  "龙胆", "杀↔闪",        4},
    {CHAR_LV_BU,     "吕布",  "无双", "杀需2张闪抵消",  5},
    {CHAR_ZHOU_YU,   "周瑜",  "英姿", "摸牌+1",        3},
    {CHAR_HUA_TUO,   "华佗",  "青囊", "弃牌回1血",   3},
};

/* ============ 玩家 ============ */
#define MAX_HAND 20
#define EQUIP_SLOTS 1

typedef struct {
    char_type_t character;
    const char *name;
    int hp;
    int max_hp;
    card_t hand[MAX_HAND];
    int hand_count;
    card_t equipment[EQUIP_SLOTS];
    int equip_count;
    card_t judge_zone[2];
    int judge_count;
    int alive;
    int has_played_kill;
} player_t;

/* ============ 游戏状态机 ============ */
typedef enum {
    PHASE_SELECT_CHAR,
    PHASE_PLAYER_TURN_DRAW,
    PHASE_PLAYER_TURN_PLAY,
    PHASE_PLAYER_TURN_DISCARD,
    PHASE_AI_TURN,
    PHASE_GAME_OVER
} game_phase_t;

/* ============ 游戏全局状态 ============ */
static lv_obj_t *sg_parent = NULL;
static player_t players[2];
static int current_player;
static card_t *deck = NULL;
static int deck_size = 0;
static int deck_capacity = 0;
static int deck_pos = 0;
static game_phase_t phase = PHASE_SELECT_CHAR;
static int turn_count = 0;
static char log_text[256];

/* LVGL UI 控件 */
static lv_obj_t *opponent_info = NULL;
static lv_obj_t *opponent_hand_container = NULL;
static lv_obj_t *status_bar = NULL;
static lv_obj_t *log_label = NULL;
static lv_obj_t *player_hand_container = NULL;
static lv_obj_t *action_bar = NULL;
static lv_obj_t *player_info = NULL;
static lv_obj_t *result_label = NULL;
static lv_obj_t *sanbg_bmp = NULL;
lv_obj_t *san_exit_btn = NULL;

#define MAX_CARD_BTNS 20
static lv_obj_t *card_btns[MAX_CARD_BTNS];
static lv_obj_t *opponent_card_btns[MAX_CARD_BTNS];
static lv_obj_t *card_imgs[MAX_CARD_BTNS];
static lv_obj_t *card_labels[MAX_CARD_BTNS];
static lv_obj_t *opponent_imgs[MAX_CARD_BTNS];
static lv_obj_t *ai_equip_img = NULL;
static lv_obj_t *player_equip_img = NULL;
static lv_obj_t *opponent_more = NULL;
static card_type_t last_card_type[MAX_CARD_BTNS];
static card_type_t last_ai_equip = CARD_NONE;
static card_type_t last_player_equip = CARD_NONE;

/* ============ 牌堆生成 ============ */
#define TOTAL_CARDS 82

static void generate_deck(void)
{
    if (deck) free(deck);
    deck_capacity = TOTAL_CARDS;
    deck = (card_t *)malloc(sizeof(card_t) * deck_capacity);
    deck_size = 0;
    card_suit_t suits[] = {SUIT_SPADE, SUIT_HEART, SUIT_CLUB, SUIT_DIAMOND};

    for (int i = 0; i < 32; i++) {
        deck[deck_size].type = CARD_KILL;
        deck[deck_size].suit = suits[i % 4];
        deck[deck_size].number = (i % 13) + 1; deck_size++;
    }
    for (int i = 0; i < 16; i++) {
        deck[deck_size].type = CARD_DODGE;
        deck[deck_size].suit = (i < 8) ? SUIT_DIAMOND : SUIT_HEART;
        deck[deck_size].number = (i % 13) + 1; deck_size++;
    }
    for (int i = 0; i < 8; i++) {
        deck[deck_size].type = CARD_PEACH;
        deck[deck_size].suit = SUIT_HEART;
        deck[deck_size].number = ((i * 2) % 13) + 1; deck_size++;
    }
    for (int i = 0; i < 5; i++) {
        deck[deck_size].type = CARD_WUZHONG;
        deck[deck_size].suit = suits[i % 4];
        deck[deck_size].number = (i + 1); deck_size++;
    }
    for (int i = 0; i < 4; i++) {
        deck[deck_size].type = CARD_DISMANTLE;
        deck[deck_size].suit = suits[i % 4];
        deck[deck_size].number = (i + 3) % 13 + 1; deck_size++;
    }
    for (int i = 0; i < 4; i++) {
        deck[deck_size].type = CARD_STEAL;
        deck[deck_size].suit = suits[i % 4];
        deck[deck_size].number = (i + 7) % 13 + 1; deck_size++;
    }
    for (int i = 0; i < 3; i++) {
        deck[deck_size].type = CARD_BARBARIAN;
        deck[deck_size].suit = suits[i % 4];
        deck[deck_size].number = (i + 1) * 4; deck_size++;
    }
    for (int i = 0; i < 3; i++) {
        deck[deck_size].type = CARD_VOLLEY;
        deck[deck_size].suit = suits[i % 4];
        deck[deck_size].number = (i + 1) * 3; deck_size++;
    }
    for (int i = 0; i < 2; i++) {
        deck[deck_size].type = CARD_PEACE;
        deck[deck_size].suit = SUIT_HEART;
        deck[deck_size].number = (i == 0) ? 1 : 13; deck_size++;
    }
    for (int i = 0; i < 3; i++) {
        deck[deck_size].type = CARD_DUEL;
        deck[deck_size].suit = suits[i % 4];
        deck[deck_size].number = (i + 5) % 13 + 1; deck_size++;
    }
    for (int i = 0; i < 2; i++) {
        deck[deck_size].type = CARD_BAGUA;
        deck[deck_size].suit = suits[i % 2];
        deck[deck_size].number = (i == 0) ? 2 : 8; deck_size++;
    }

    srand((unsigned)time(NULL));
    for (int i = deck_size - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        card_t tmp = deck[i];
        deck[i] = deck[j];
        deck[j] = tmp;
    }
    deck_pos = 0;
}
/* ============ 辅助函数 ============ */

static const char* card_name(card_type_t type)
{
    switch (type) {
        case CARD_KILL:      return "杀";
        case CARD_DODGE:     return "闪";
        case CARD_PEACH:     return "桃";
        case CARD_WUZHONG:   return "无中生有";
        case CARD_DISMANTLE: return "过河拆桥";
        case CARD_STEAL:     return "顺手牵羊";
        case CARD_BARBARIAN: return "南蛮入侵";
        case CARD_VOLLEY:    return "万箭齐发";
        case CARD_PEACE:     return "桃园结义";
        case CARD_DUEL:      return "决斗";
        case CARD_BAGUA:     return "八卦阵";
        case CARD_HAPPINESS: return "乐不思蜀";
        case CARD_THUNDER:   return "闪电";
        default: return "?";
    }
}

static const char* suit_str(card_suit_t suit)
{
    switch (suit) {
        case SUIT_SPADE:   return "黑桃";
        case SUIT_HEART:   return "红桃";
        case SUIT_CLUB:    return "梅花";
        case SUIT_DIAMOND: return "方片";
        default: return "?";
    }
}

static const char* num_str(int n)
{
    switch (n) {
        case  1: return "A";
        case 11: return "J";
        case 12: return "Q";
        case 13: return "K";
        default: { static char buf[4]; snprintf(buf,sizeof(buf),"%d",n); return buf; }
    }
}

static card_t draw_card(void)
{
    if (deck_pos >= deck_size) {
        card_t empty = {CARD_NONE, SUIT_SPADE, 0};
        return empty;
    }
    return deck[deck_pos++];
}

static void add_to_hand(player_t *p, card_t c)
{
    if (p->hand_count < MAX_HAND) p->hand[p->hand_count++] = c;
}

static void remove_from_hand(player_t *p, int idx)
{
    if (idx < 0 || idx >= p->hand_count) return;
    for (int i = idx; i < p->hand_count - 1; i++) p->hand[i] = p->hand[i + 1];
    p->hand_count--;
}

static int deal_damage(player_t *target, int amount)
{
    target->hp -= amount;
    if (target->hp < 0) target->hp = 0;
    if (target->hp == 0) target->alive = 0;
    return 1;
}

static void heal(player_t *p, int amount)
{
    p->hp += amount;
    if (p->hp > p->max_hp) p->hp = p->max_hp;
}

static int can_play_kill(player_t *p)
{
    if (p->character == CHAR_ZHANG_FEI) return 1;
    return !p->has_played_kill;
}

static void init_game(int player_char, int ai_char)
{
    players[0].character = (char_type_t)player_char;
    players[0].name = char_defs[player_char].name;
    players[0].max_hp = char_defs[player_char].max_hp;
    players[0].hp = players[0].max_hp;
    players[0].hand_count = 0;
    players[0].equip_count = 0;
    players[0].judge_count = 0;
    players[0].alive = 1;
    players[0].has_played_kill = 0;

    players[1].character = (char_type_t)ai_char;
    players[1].name = char_defs[ai_char].name;
    players[1].max_hp = char_defs[ai_char].max_hp;
    players[1].hp = players[1].max_hp;
    players[1].hand_count = 0;
    players[1].equip_count = 0;
    players[1].judge_count = 0;
    players[1].alive = 1;
    players[1].has_played_kill = 0;

    generate_deck();
    for (int i = 0; i < 4; i++) {
        add_to_hand(&players[0], draw_card());
        add_to_hand(&players[1], draw_card());
    }
    current_player = 0;
    turn_count = 0;
    log_text[0] = '\0';
}

static void set_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(log_text, sizeof(log_text), fmt, args);
    va_end(args);
}

static int play_kill(player_t *user, player_t *target, int card_idx)
{
    if (!can_play_kill(user)) { set_log("本回合已出过杀"); return 1; }
    int dodge_needed = (user->character == CHAR_LV_BU) ? 2 : 1;

    int has_bagua = 0;
    for (int i = 0; i < target->equip_count; i++)
        if (target->equipment[i].type == CARD_BAGUA) { has_bagua = 1; break; }

    int dodged = 0;
    if (has_bagua) {
        int judge = rand() % 2;
        if (judge) { dodged = 1; set_log("八卦阵判定成功"); }
    }

    if (!dodged) {
        int dodge_count = 0;
        for (int i = 0; i < target->hand_count; i++)
            if (target->hand[i].type == CARD_DODGE) dodge_count++;
        int zy_dodge = 0;
        if (target->character == CHAR_ZHAO_YUN)
            for (int i = 0; i < target->hand_count; i++)
                if (target->hand[i].type == CARD_KILL) zy_dodge++;

        if (dodge_count + zy_dodge >= dodge_needed) {
            int consumed = 0;
            for (int i = 0; i < target->hand_count && consumed < dodge_needed; i++)
                if (target->hand[i].type == CARD_DODGE) { remove_from_hand(target,i); i--; consumed++; }
            if (target->character == CHAR_ZHAO_YUN)
                for (int i = 0; i < target->hand_count && consumed < dodge_needed; i++)
                    if (target->hand[i].type == CARD_KILL) { remove_from_hand(target,i); i--; consumed++; }
            dodged = 1;
        }
    }

    remove_from_hand(user, card_idx);
    user->has_played_kill = 1;
    if (!dodged) {
        deal_damage(target, 1);
        set_log("%s 对 %s 造成1点伤害", user->name, target->name);
    } else {
        set_log("%s 的杀被 %s 闪避", user->name, target->name);
    }
    return 0;
}

/* ============ 应用卡牌效果 ============ */
static int apply_card(player_t *user, player_t *target, int card_idx)
{
    if (card_idx < 0 || card_idx >= user->hand_count) return 1;
    card_t c = user->hand[card_idx];

    switch (c.type) {
    case CARD_KILL:
        return play_kill(user, target, card_idx);
    case CARD_DODGE:
        if (user->character == CHAR_ZHAO_YUN)
            return play_kill(user, target, card_idx);
        set_log("闪不能在出牌阶段主动使用"); return 1;
    case CARD_PEACH: {
        if (user->hp >= user->max_hp) { set_log("体力已满"); return 1; }
        remove_from_hand(user, card_idx);
        heal(user, 1);
        set_log("%s 吃桃恢复1点体力", user->name);
        return 0;
    }
    case CARD_WUZHONG: {
        remove_from_hand(user, card_idx);
        add_to_hand(user, draw_card());
        add_to_hand(user, draw_card());
        set_log("%s 使用无中生有, 摸2张牌", user->name);
        return 0;
    }
    case CARD_DISMANTLE: {
        if (target->hand_count == 0 && target->equip_count == 0) { set_log("目标没有可拆的牌"); return 1; }
        remove_from_hand(user, card_idx);
        if (target->equip_count > 0) target->equip_count--;
        else remove_from_hand(target, 0);
        set_log("%s 拆掉了 %s 的一张牌", user->name, target->name);
        return 0;
    }
    case CARD_STEAL: {
        if (target->hand_count == 0 && target->equip_count == 0) { set_log("目标没有可拿的牌"); return 1; }
        remove_from_hand(user, card_idx);
        if (target->equip_count > 0) {
            target->equip_count--;
            add_to_hand(user, draw_card());
        } else {
            card_t s = target->hand[0];
            remove_from_hand(target, 0);
            add_to_hand(user, s);
        }
        set_log("%s 顺走了 %s 的一张牌", user->name, target->name);
        return 0;
    }
    case CARD_BARBARIAN: {
        remove_from_hand(user, card_idx);
        int has_kill = 0;
        for (int i = 0; i < target->hand_count; i++)
            if (target->hand[i].type == CARD_KILL) { has_kill = 1; remove_from_hand(target,i); break; }
        if (!has_kill) { deal_damage(target, 1); set_log("南蛮入侵! %s 受到1点伤害", target->name); }
        else set_log("%s 用杀抵挡了南蛮入侵",target->name);
        return 0;
    }
    case CARD_VOLLEY: {
        remove_from_hand(user, card_idx);
        int has_dodge = 0;
        for (int i = 0; i < target->hand_count; i++)
            if (target->hand[i].type == CARD_DODGE) { has_dodge = 1; remove_from_hand(target,i); break; }
        if (!has_dodge && target->character == CHAR_ZHAO_YUN)
            for (int i = 0; i < target->hand_count; i++)
                if (target->hand[i].type == CARD_KILL) { has_dodge = 1; remove_from_hand(target,i); break; }
        if (!has_dodge) { deal_damage(target, 1); set_log("万箭齐发! %s 受到1点伤害", target->name); }
        else set_log("%s 用闪抵挡了万箭齐发",target->name);
        return 0;
    }
    case CARD_PEACE: {
        remove_from_hand(user, card_idx);
        heal(user, user->max_hp - user->hp);
        heal(target, target->max_hp - target->hp);
        set_log("桃园结义! 全体恢复体力");
        return 0;
    }
    case CARD_DUEL: {
        remove_from_hand(user, card_idx);
        int u_kills = 0, t_kills = 0;
        for (int i = 0; i < user->hand_count; i++) if (user->hand[i].type == CARD_KILL) u_kills++;
        for (int i = 0; i < target->hand_count; i++) if (target->hand[i].type == CARD_KILL) t_kills++;
        if (u_kills >= t_kills) { deal_damage(target, 1); set_log("决斗! %s 受到1点伤害", target->name); }
        else { deal_damage(user, 1); set_log("决斗! %s 反击1点伤害", user->name); }
        return 0;
    }
    case CARD_BAGUA: {
        if (user->equip_count >= EQUIP_SLOTS) { set_log("装备区已满"); return 1; }
        remove_from_hand(user, card_idx);
        user->equipment[user->equip_count++] = c;
        set_log("%s 装备了八卦阵", user->name);
        return 0;
    }
    default: return 1;
    }
}
/* ============ AI 逻辑 ============ */
static int ai_card_priority(card_type_t t)
{
    switch (t) {
        case CARD_PEACH:     return 90;
        case CARD_WUZHONG:   return 80;
        case CARD_STEAL:     return 70;
        case CARD_DISMANTLE: return 65;
        case CARD_KILL:      return 60;
        case CARD_DUEL:      return 55;
        case CARD_BARBARIAN: return 50;
        case CARD_VOLLEY:    return 50;
        case CARD_PEACE:     return 85;
        case CARD_BAGUA:     return 45;
        default: return 0;
    }
}

static void ai_take_turn(void)
{
    player_t *ai = &players[1];
    player_t *player = &players[0];

    int draw_count = (ai->character == CHAR_ZHOU_YU) ? 3 : 2;
    for (int i = 0; i < draw_count; i++) {
        card_t c = draw_card();
        if (c.type != CARD_NONE) add_to_hand(ai, c);
    }

    int acted = 1;
    while (acted) {
        acted = 0;
        int best = -1, best_idx = -1;
        for (int i = 0; i < ai->hand_count; i++) {
            int p = ai_card_priority(ai->hand[i].type);
            if (ai->hand[i].type == CARD_KILL && !can_play_kill(ai)) continue;
            if (ai->hand[i].type == CARD_PEACH && ai->hp >= ai->max_hp) continue;
            if (ai->hand[i].type == CARD_BAGUA && ai->equip_count > 0) continue;
            if ((ai->hand[i].type == CARD_DISMANTLE || ai->hand[i].type == CARD_STEAL) &&
                player->hand_count == 0 && player->equip_count == 0) continue;
            if (p > best) { best = p; best_idx = i; }
        }
        if (best_idx < 0) break;
        card_t c = ai->hand[best_idx];

        switch (c.type) {
        case CARD_PEACH: remove_from_hand(ai, best_idx); heal(ai, 1); acted = 1; break;
        case CARD_BAGUA:
            if (ai->equip_count < EQUIP_SLOTS) { remove_from_hand(ai, best_idx); ai->equipment[ai->equip_count++] = c; acted = 1; }
            break;
        case CARD_KILL: {
            int player_bagua = 0;
            for (int j = 0; j < player->equip_count; j++)
                if (player->equipment[j].type == CARD_BAGUA) player_bagua = 1;
            int dn = (ai->character == CHAR_LV_BU) ? 2 : 1;
            int pd = 0;
            for (int j = 0; j < player->hand_count; j++) {
                if (player->hand[j].type == CARD_DODGE) pd++;
                if (player->character == CHAR_ZHAO_YUN && player->hand[j].type == CARD_KILL) pd++;
            }
            if (!player_bagua && pd < dn) {
                remove_from_hand(ai, best_idx);
                ai->has_played_kill = 1;
                deal_damage(player, 1); acted = 1;
            } else {
                remove_from_hand(ai, best_idx);
                ai->has_played_kill = 1;
                int con = 0;
                for (int j = 0; j < player->hand_count && con < dn; j++)
                    if (player->hand[j].type == CARD_DODGE) { remove_from_hand(player,j); j--; con++; }
                if (player->character == CHAR_ZHAO_YUN)
                    for (int j = 0; j < player->hand_count && con < dn; j++)
                        if (player->hand[j].type == CARD_KILL) { remove_from_hand(player,j); j--; con++; }
                acted = 1;
            }
            break;
        }
        case CARD_WUZHONG:
            remove_from_hand(ai, best_idx);
            add_to_hand(ai, draw_card()); add_to_hand(ai, draw_card());
            acted = 1; break;
        case CARD_DISMANTLE:
            remove_from_hand(ai, best_idx);
            if (player->equip_count > 0) player->equip_count--;
            else if (player->hand_count > 0) remove_from_hand(player, 0);
            acted = 1; break;
        case CARD_STEAL:
            remove_from_hand(ai, best_idx);
            if (player->equip_count > 0) { player->equip_count--; add_to_hand(ai, draw_card()); }
            else if (player->hand_count > 0) { card_t s = player->hand[0]; remove_from_hand(player,0); add_to_hand(ai,s); }
            acted = 1; break;
        case CARD_BARBARIAN: {
            remove_from_hand(ai, best_idx);
            int hk = 0;
            for (int j = 0; j < player->hand_count; j++)
                if (player->hand[j].type == CARD_KILL) { hk = 1; remove_from_hand(player,j); break; }
            if (!hk) deal_damage(player, 1);
            acted = 1; break;
        }
        case CARD_VOLLEY: {
            remove_from_hand(ai, best_idx);
            int hd = 0;
            for (int j = 0; j < player->hand_count; j++)
                if (player->hand[j].type == CARD_DODGE) { hd = 1; remove_from_hand(player,j); break; }
            if (!hd && player->character == CHAR_ZHAO_YUN)
                for (int j = 0; j < player->hand_count; j++)
                    if (player->hand[j].type == CARD_KILL) { hd = 1; remove_from_hand(player,j); break; }
            if (!hd) deal_damage(player, 1);
            acted = 1; break;
        }
        case CARD_PEACE:
            remove_from_hand(ai, best_idx);
            heal(ai, ai->max_hp - ai->hp);
            heal(player, player->max_hp - player->hp);
            acted = 1; break;
        case CARD_DUEL: {
            remove_from_hand(ai, best_idx);
            int ak = 0, pk = 0;
            for (int j = 0; j < ai->hand_count; j++) if (ai->hand[j].type == CARD_KILL) ak++;
            for (int j = 0; j < player->hand_count; j++) if (player->hand[j].type == CARD_KILL) pk++;
            if (ak >= pk) deal_damage(player, 1); else deal_damage(ai, 1);
            acted = 1; break;
        }
        default: break;
        }
    }

    while (ai->hand_count > ai->hp) {
        int di = -1;
        for (int i = 0; i < ai->hand_count; i++)
            if (ai->hand[i].type == CARD_KILL) { di = i; break; }
        if (di < 0) di = 0;
        remove_from_hand(ai, di);
    }
    ai->has_played_kill = 0;
}
/* ============ LVGL UI 函数 ============ */

static void clear_screen(void)
{
    if (!sg_parent) return;
    uint32_t child_cnt = lv_obj_get_child_cnt(sg_parent);
    for (int i = (int)child_cnt - 1; i >= 0; i--) {
        lv_obj_t *child = lv_obj_get_child(sg_parent, i);
        if (child != sanbg_bmp && child != san_exit_btn) lv_obj_del(child);
    }
    opponent_info = NULL; opponent_hand_container = NULL;
    status_bar = NULL; log_label = NULL;
    player_hand_container = NULL; action_bar = NULL;
    player_info = NULL; result_label = NULL;
    for (int i = 0; i < MAX_CARD_BTNS; i++) {
        card_btns[i] = NULL; opponent_card_btns[i] = NULL;
        card_imgs[i] = NULL; card_labels[i] = NULL; opponent_imgs[i] = NULL;
        last_card_type[i] = CARD_NONE;
    }
    ai_equip_img = NULL; player_equip_img = NULL; opponent_more = NULL;
    last_ai_equip = CARD_NONE; last_player_equip = CARD_NONE;
}

/* ============ 图片资源 ============ */
static const char* card_img_path(card_type_t t)
{
    switch (t) {
        case CARD_KILL:      return SAN_IMG_DIR "card_kill.bmp";
        case CARD_DODGE:     return SAN_IMG_DIR "card_dodge.bmp";
        case CARD_PEACH:     return SAN_IMG_DIR "card_peach.bmp";
        case CARD_WUZHONG:   return SAN_IMG_DIR "card_wuzhong.bmp";
        case CARD_DISMANTLE: return SAN_IMG_DIR "card_dismantle.bmp";
        case CARD_STEAL:     return SAN_IMG_DIR "card_steal.bmp";
        case CARD_BARBARIAN: return SAN_IMG_DIR "card_barbarian.bmp";
        case CARD_VOLLEY:    return SAN_IMG_DIR "card_volley.bmp";
        case CARD_PEACE:     return SAN_IMG_DIR "card_peace.bmp";
        case CARD_DUEL:      return SAN_IMG_DIR "card_duel.bmp";
        case CARD_BAGUA:     return SAN_IMG_DIR "card_bagua.bmp";
        case CARD_HAPPINESS: return SAN_IMG_DIR "card_happiness.bmp";
        case CARD_THUNDER:   return SAN_IMG_DIR "card_thunder.bmp";
        default:             return SAN_IMG_DIR "cardback.bmp";
    }
}

static const char* char_img_path(char_type_t t)
{
    switch (t) {
        case CHAR_GUAN_YU:   return SAN_IMG_DIR "char_guanyu.bmp";
        case CHAR_ZHANG_FEI: return SAN_IMG_DIR "char_zhangfei.bmp";
        case CHAR_ZHAO_YUN:  return SAN_IMG_DIR "char_zhaoyun.bmp";
        case CHAR_LV_BU:     return SAN_IMG_DIR "char_lvbu.bmp";
        case CHAR_ZHOU_YU:   return SAN_IMG_DIR "char_zhouyu.bmp";
        case CHAR_HUA_TUO:   return SAN_IMG_DIR "char_huatuo.bmp";
        default:             return SAN_IMG_DIR "cardback.bmp";
    }
}

static lv_obj_t* create_card_img(lv_obj_t *parent, const char *src, int dst_w, int dst_h)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, src);
    lv_obj_set_size(img, dst_w, dst_h);
    return img;
}

static void style_card_button(lv_obj_t *btn)
{
    lv_obj_set_size(btn, CARD_W, CARD_H);
    lv_obj_set_style_radius(btn, 5, 0);
    lv_obj_set_style_shadow_width(btn, 4, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
}

/* ============ 武将选择界面 ============ */
static void start_game_with_chars(int player_char, int ai_char);
static void create_character_select(void);

static void char_select_cb(lv_event_t *e)
{
    int char_idx = (int)(intptr_t)lv_event_get_user_data(e);
    int ai_char; do { ai_char = rand() % CHAR_COUNT; } while (ai_char == char_idx);
    start_game_with_chars(char_idx, ai_char);
}

static void create_character_select(void)
{
    clear_screen();
    phase = PHASE_SELECT_CHAR;

    lv_obj_t *title = lv_label_create(sg_parent);
    lv_label_set_text(title, "选择你的武将");
    lv_obj_set_style_text_font(title, &lv_sanguo_font_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    int total_w = CHAR_COUNT * CARD_W + (CHAR_COUNT - 1) * CHAR_CARD_GAP;
    int start_x = (1024 - total_w) / 2;
    int start_y = 190;

    for (int i = 0; i < CHAR_COUNT; i++) {
        lv_obj_t *card = lv_btn_create(sg_parent);
        lv_obj_set_size(card, CARD_W, CARD_H);
        lv_obj_set_pos(card, start_x + i * (CARD_W + CHAR_CARD_GAP), start_y);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_style_shadow_width(card, 8, 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0x444444), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(card, char_select_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t *img = create_card_img(card, char_img_path((char_type_t)i), CARD_W, CARD_H);
        lv_obj_center(img);
    }
}

/* ============ 游戏界面 ============ */

static void update_opponent_info(void)
{
    if (!opponent_info) return;
    player_t *ai = &players[1];
    char buf[128], hp_bar[32] = "";
    for (int i = 0; i < ai->hp; i++) strcat(hp_bar, "♥");
    for (int i = ai->hp; i < ai->max_hp; i++) strcat(hp_bar, "♡");
    snprintf(buf, sizeof(buf), "AI %s %s %d/%d 牌:%d", ai->name, hp_bar, ai->hp, ai->max_hp, ai->hand_count);
    lv_label_set_text(opponent_info, buf);
}

static void update_player_info(void)
{
    if (!player_info) return;
    player_t *p = &players[0];
    char buf[128], hp_bar[32] = "", equip_str[32] = "";
    for (int i = 0; i < p->hp; i++) strcat(hp_bar, "♥");
    for (int i = p->hp; i < p->max_hp; i++) strcat(hp_bar, "♡");
    for (int i = 0; i < p->equip_count; i++) { strcat(equip_str, "["); strcat(equip_str, card_name(p->equipment[i].type)); strcat(equip_str, "] "); }
    snprintf(buf, sizeof(buf), "%s %s %s %d/%d", p->name, hp_bar, equip_str, p->hp, p->max_hp);
    lv_label_set_text(player_info, buf);
}

static void update_status_bar(void)
{
    if (!status_bar) return;
    player_t *ai = &players[1];
    char buf[128], ai_equip[32] = "";
    for (int i = 0; i < ai->equip_count; i++) { strcat(ai_equip, "["); strcat(ai_equip, card_name(ai->equipment[i].type)); strcat(ai_equip, "] "); }
    const char *ps = "";
    switch (phase) {
        case PHASE_PLAYER_TURN_PLAY:   ps = "出牌阶段"; break;
        case PHASE_PLAYER_TURN_DISCARD:ps = "弃牌阶段"; break;
        case PHASE_AI_TURN:            ps = "AI回合"; break;
        default: ps = "";
    }
    snprintf(buf, sizeof(buf), "第%d回合 %s AI装备: %s", turn_count, ps, ai_equip);
    lv_label_set_text(status_bar, buf);
}

static void update_log(void) { if (log_label) lv_label_set_text(log_label, log_text); }

/* 卡牌点击回调 */
static void update_game_ui(void);
static void card_click_cb(lv_event_t *e)
{
    int card_idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (phase != PHASE_PLAYER_TURN_PLAY && phase != PHASE_PLAYER_TURN_DISCARD) return;
    if (current_player != 0) return;
    player_t *p = &players[0], *ai = &players[1];

    if (phase == PHASE_PLAYER_TURN_DISCARD) {
        if (card_idx >= 0 && card_idx < p->hand_count) remove_from_hand(p, card_idx);
        update_game_ui(); return;
    }

    if (card_idx < 0 || card_idx >= p->hand_count) return;
    int ret = apply_card(p, ai, card_idx);
    update_game_ui();
    if (ret == 0 && !ai->alive) { phase = PHASE_GAME_OVER; update_game_ui(); }
}

static void end_turn_cb(lv_event_t *e) { (void)e; if (phase == PHASE_PLAYER_TURN_PLAY) { phase = PHASE_PLAYER_TURN_DISCARD; update_game_ui(); } }

static void confirm_discard_cb(lv_event_t *e)
{
    (void)e;
    player_t *p = &players[0];
    if (p->hand_count > p->hp) { set_log("还需要弃%d张牌", p->hand_count - p->hp); update_game_ui(); return; }
    p->has_played_kill = 0; current_player = 1; phase = PHASE_AI_TURN;
    set_log("AI回合..."); update_game_ui();
    ai_take_turn();
    if (!players[0].alive) { phase = PHASE_GAME_OVER; update_game_ui(); return; }
    current_player = 0; turn_count++;
    int draw = (p->character == CHAR_ZHOU_YU) ? 3 : 2;
    for (int i = 0; i < draw; i++) { card_t c = draw_card(); if (c.type != CARD_NONE) add_to_hand(p, c); }
    phase = PHASE_PLAYER_TURN_PLAY;
    set_log("你的回合 - 出牌阶段"); update_game_ui();
}

/* 更新整个游戏界面 */
static void update_game_ui(void)
{
    if (phase == PHASE_GAME_OVER) {
        clear_screen();
        result_label = lv_label_create(sg_parent);
        if (players[1].alive == 0) lv_label_set_text(result_label, "胜利! 你击败了AI!");
        else if (players[0].alive == 0) lv_label_set_text(result_label, "失败! 你被AI击败!");
        else lv_label_set_text(result_label, "游戏结束");
        lv_obj_set_style_text_font(result_label, &lv_sanguo_font_24, 0);
        lv_obj_set_style_text_color(result_label, lv_color_hex(0xFFD700), 0);
        lv_obj_center(result_label);
        lv_obj_t *restart_btn = lv_btn_create(sg_parent);
        lv_obj_set_size(restart_btn, 120, 40);
        lv_obj_align(restart_btn, LV_ALIGN_CENTER, 0, 50);
        lv_obj_t *r_lbl = lv_label_create(restart_btn);
    lv_label_set_text(r_lbl, "再来一局");
        lv_obj_center(r_lbl);
        lv_obj_set_style_text_font(r_lbl, &lv_sanguo_font_24, 0);
        lv_obj_add_event_cb(restart_btn, (lv_event_cb_t)(void*)create_character_select, LV_EVENT_CLICKED, NULL);
        return;
    }
    if (phase == PHASE_AI_TURN) return;

    player_t *p = &players[0], *ai = &players[1];
    update_opponent_info(); update_player_info(); update_status_bar(); update_log();

    int ox = 0;
    if (ai->equip_count > 0) {
        lv_obj_remove_flag(ai_equip_img, LV_OBJ_FLAG_HIDDEN);
        if (ai->equipment[0].type != last_ai_equip) {
            lv_image_set_src(ai_equip_img, card_img_path(ai->equipment[0].type));
            last_ai_equip = ai->equipment[0].type;
        }
        lv_obj_set_pos(ai_equip_img, ox, 0);
        ox += AI_CARD_GAP;
    } else {
        lv_obj_add_flag(ai_equip_img, LV_OBJ_FLAG_HIDDEN);
        last_ai_equip = CARD_NONE;
    }
    int sc = (ai->hand_count > 8) ? 8 : ai->hand_count;
    for (int i = 0; i < sc; i++) {
        lv_obj_remove_flag(opponent_card_btns[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(opponent_card_btns[i], ox, 0);
        ox += AI_CARD_GAP;
    }
    for (int i = sc; i < MAX_CARD_BTNS; i++)
        lv_obj_add_flag(opponent_card_btns[i], LV_OBJ_FLAG_HIDDEN);
    if (ai->hand_count > 8) {
        char buf[16]; snprintf(buf, sizeof(buf), "+%d", ai->hand_count - 8);
        lv_label_set_text(opponent_more, buf);
        lv_obj_remove_flag(opponent_more, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(opponent_more, ox + 4, 4);
    } else {
        lv_obj_add_flag(opponent_more, LV_OBJ_FLAG_HIDDEN);
    }

    if (p->equip_count > 0) {
        lv_obj_remove_flag(player_equip_img, LV_OBJ_FLAG_HIDDEN);
        if (p->equipment[0].type != last_player_equip) {
            lv_image_set_src(player_equip_img, card_img_path(p->equipment[0].type));
            last_player_equip = p->equipment[0].type;
        }
    } else {
        lv_obj_add_flag(player_equip_img, LV_OBJ_FLAG_HIDDEN);
        last_player_equip = CARD_NONE;
    }
    for (int i = 0; i < p->hand_count && i < MAX_CARD_BTNS; i++) {
        lv_obj_remove_flag(card_btns[i], LV_OBJ_FLAG_HIDDEN);
        if (p->hand[i].type != last_card_type[i]) {
            lv_image_set_src(card_imgs[i], card_img_path(p->hand[i].type));
            last_card_type[i] = p->hand[i].type;
        }
        char sn[16]; snprintf(sn, sizeof(sn), "%s%s", suit_str(p->hand[i].suit), num_str(p->hand[i].number));
        lv_label_set_text(card_labels[i], sn);
        uint32_t txt_color = (p->hand[i].suit == SUIT_HEART || p->hand[i].suit == SUIT_DIAMOND) ? 0xFF5555 : 0xFFFFFF;
        lv_obj_set_style_text_color(card_labels[i], lv_color_hex(txt_color), 0);
    }
    for (int i = p->hand_count; i < MAX_CARD_BTNS; i++) {
        lv_obj_add_flag(card_btns[i], LV_OBJ_FLAG_HIDDEN);
        last_card_type[i] = CARD_NONE;
    }

    lv_obj_clean(action_bar);
    if (phase == PHASE_PLAYER_TURN_PLAY) {
        lv_obj_t *btn = lv_btn_create(action_bar);
        lv_obj_set_size(btn, 90, 36);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xCC6600), 0);
        lv_obj_set_style_radius(btn, 5, 0);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "结束回合");
        lv_obj_center(lbl);
        lv_obj_set_style_text_font(lbl, &lv_sanguo_font_24, 0);
        lv_obj_add_event_cb(btn, end_turn_cb, LV_EVENT_CLICKED, NULL);
    }
    if (phase == PHASE_PLAYER_TURN_DISCARD) {
        int nd = p->hand_count - p->hp;
        if (nd > 0) {
            char buf[48]; snprintf(buf, sizeof(buf), "需要弃%d张牌(点击手牌)", nd);
            lv_obj_t *lbl = lv_label_create(action_bar);
            lv_label_set_text(lbl, buf);
            lv_obj_set_style_text_font(lbl, &lv_sanguo_font_24, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFF00), 0);
        } else {
            lv_obj_t *btn = lv_btn_create(action_bar);
            lv_obj_set_size(btn, 90, 36);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x336633), 0);
            lv_obj_set_style_radius(btn, 5, 0);
            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, "确认");
            lv_obj_center(lbl);
            lv_obj_set_style_text_font(lbl, &lv_sanguo_font_24, 0);
            lv_obj_add_event_cb(btn, confirm_discard_cb, LV_EVENT_CLICKED, NULL);
        }
    }
}

/* 创建游戏主界面 */
static void create_game_ui(void)
{
    clear_screen();

    opponent_hand_container = lv_obj_create(sg_parent);
    lv_obj_set_size(opponent_hand_container, 900, CARD_H);
    lv_obj_set_pos(opponent_hand_container, 0, 0);
    lv_obj_set_style_pad_all(opponent_hand_container, 0, 0);
    lv_obj_set_style_border_width(opponent_hand_container, 0, 0);
    lv_obj_set_style_bg_opa(opponent_hand_container, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(opponent_hand_container, LV_OBJ_FLAG_SCROLLABLE);

    player_hand_container = lv_obj_create(sg_parent);
    lv_obj_set_size(player_hand_container, 1024, CARD_H);
    lv_obj_set_pos(player_hand_container, 0, 600 - CARD_H);
    lv_obj_set_style_pad_all(player_hand_container, 0, 0);
    lv_obj_set_style_pad_column(player_hand_container, 8, 0);
    lv_obj_set_style_border_width(player_hand_container, 0, 0);
    lv_obj_set_style_bg_opa(player_hand_container, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(player_hand_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(player_hand_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(player_hand_container, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(player_hand_container, LV_SCROLLBAR_MODE_OFF);

    ai_equip_img = create_card_img(opponent_hand_container, SAN_IMG_DIR "cardback.bmp", CARD_W, CARD_H);
    lv_obj_add_flag(ai_equip_img, LV_OBJ_FLAG_HIDDEN);
    player_equip_img = create_card_img(player_hand_container, SAN_IMG_DIR "cardback.bmp", CARD_W, CARD_H);
    lv_obj_add_flag(player_equip_img, LV_OBJ_FLAG_HIDDEN);

    /* 预创建卡牌控件，刷新时只改图片/显隐，避免反复解码卡图 */
    for (int i = 0; i < MAX_CARD_BTNS; i++) {
        opponent_card_btns[i] = lv_btn_create(opponent_hand_container);
        style_card_button(opponent_card_btns[i]);
        opponent_imgs[i] = create_card_img(opponent_card_btns[i], SAN_IMG_DIR "cardback.bmp", CARD_W, CARD_H);
        lv_obj_center(opponent_imgs[i]);
        lv_obj_add_flag(opponent_card_btns[i], LV_OBJ_FLAG_HIDDEN);

        card_btns[i] = lv_btn_create(player_hand_container);
        style_card_button(card_btns[i]);
        card_imgs[i] = create_card_img(card_btns[i], SAN_IMG_DIR "cardback.bmp", CARD_W, CARD_H);
        lv_obj_center(card_imgs[i]);
        card_labels[i] = lv_label_create(card_btns[i]);
        lv_label_set_text(card_labels[i], "");
        lv_obj_set_style_text_font(card_labels[i], &lv_sanguo_font_24, 0);
        lv_obj_set_style_bg_color(card_labels[i], lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(card_labels[i], LV_OPA_50, 0);
        lv_obj_set_style_pad_all(card_labels[i], 1, 0);
        lv_obj_align(card_labels[i], LV_ALIGN_BOTTOM_MID, 0, -2);
        lv_obj_add_event_cb(card_btns[i], card_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_add_flag(card_btns[i], LV_OBJ_FLAG_HIDDEN);
    }

    opponent_more = lv_label_create(opponent_hand_container);
    lv_label_set_text(opponent_more, "");
    lv_obj_set_style_text_font(opponent_more, &lv_sanguo_font_24, 0);
    lv_obj_add_flag(opponent_more, LV_OBJ_FLAG_HIDDEN);

    opponent_info = lv_label_create(sg_parent);
    lv_obj_set_size(opponent_info, lv_pct(100), 26);
    lv_obj_set_pos(opponent_info, 0, 202);
    lv_obj_set_style_text_font(opponent_info, &lv_sanguo_font_24, 0);
    lv_obj_set_style_bg_color(opponent_info, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(opponent_info, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(opponent_info, 2, 0);

    status_bar = lv_label_create(sg_parent);
    lv_obj_set_size(status_bar, lv_pct(100), 26);
    lv_obj_set_pos(status_bar, 0, 232);
    lv_obj_set_style_text_font(status_bar, &lv_sanguo_font_24, 0);
    lv_obj_set_style_text_color(status_bar, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(status_bar, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(status_bar, 2, 0);

    log_label = lv_label_create(sg_parent);
    lv_obj_set_size(log_label, lv_pct(100), 24);
    lv_obj_set_pos(log_label, 0, 262);
    lv_obj_set_style_text_font(log_label, &lv_sanguo_font_24, 0);
    lv_obj_set_style_text_color(log_label, lv_color_hex(0xFFCC66), 0);
    lv_obj_set_style_bg_color(log_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(log_label, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(log_label, 2, 0);

    player_info = lv_label_create(sg_parent);
    lv_obj_set_size(player_info, lv_pct(100), 26);
    lv_obj_set_pos(player_info, 0, 290);
    lv_obj_set_style_text_font(player_info, &lv_sanguo_font_24, 0);
    lv_obj_set_style_bg_color(player_info, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(player_info, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(player_info, 2, 0);

    action_bar = lv_obj_create(sg_parent);
    lv_obj_set_size(action_bar, lv_pct(100), 45);
    lv_obj_set_pos(action_bar, 0, 350);
    lv_obj_set_flex_flow(action_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(action_bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(action_bar, 4, 0);
    lv_obj_set_style_border_width(action_bar, 0, 0);
    lv_obj_set_style_bg_color(action_bar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(action_bar, LV_OPA_70, 0);
}

static void start_game_with_chars(int player_char, int ai_char)
{
    init_game(player_char, ai_char);
    create_game_ui();
    player_t *p = &players[0];
    int draw = (p->character == CHAR_ZHOU_YU) ? 3 : 2;
    for (int i = 0; i < draw; i++) { card_t c = draw_card(); if (c.type != CARD_NONE) add_to_hand(p, c); }
    phase = PHASE_PLAYER_TURN_PLAY;
    set_log("游戏开始! 你的回合 - 出牌阶段");
    update_game_ui();
}

/* ============ 入口 ============ */
void draw_sanguosha_on_screen(lv_obj_t *parent)
{
    sg_parent = parent;
    if (sanbg_bmp) { lv_obj_del(sanbg_bmp); sanbg_bmp = NULL; }
    sanbg_bmp = lv_image_create(sg_parent);
    lv_image_set_src(sanbg_bmp, SAN_IMG_DIR "sanbg.bmp");
    lv_obj_set_size(sanbg_bmp, 1024, 600);
    lv_obj_set_pos(sanbg_bmp, 0, 0);

    srand((unsigned)time(NULL));
    create_character_select();
}











