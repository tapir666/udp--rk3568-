#ifndef __MAIN_SRC_H__
#define __MAIN_SRC_H__



typedef struct {
    lv_style_t *style;
    lv_color_t orig_color;
} btn_style_data_t;

typedef struct {
    lv_color_t       color;
    lv_align_t       align;
    lv_obj_t        *obj;         // 容器
    lv_obj_t        *btn;         // 按钮
    lv_obj_t        *label;       // 标签  
    lv_obj_t        *bmp;         // 背景图
    lv_style_t       bg_style;    // 容器背景
    lv_style_t       btn_style;   // 按钮背景
    lv_style_t       lab_style;   // 标签背景
    lv_style_t       bmp_style;   //图片样式
    btn_style_data_t evt;         // 回调用的数据
} slot_t;

void main_src_init(void);


#endif


