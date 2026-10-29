#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "../../lvgl/lvgl.h"
#include "game_2048.h"

int arr_2048[4][4] = {0};
static lv_style_t obj_2048_style;
lv_obj_t *image_2048[4][4]; //保存2048显示的图片控件
#define PIC_SAVE_PATH "A:/tapir/bmp_pic"
lv_point_t start_point, end_point;
static lv_obj_t *game_result_label = NULL;   // 结果标签
static int game_active = 1;                   // 0=游戏结束/胜利, 1=进行中
static lv_obj_t *obj_2048 = NULL;
extern lv_obj_t *c_src;


void draw_2048_on_screen(lv_obj_t *parent)
{
    // obj_2048 = parent;
    // game_2048();
    obj_2048 = lv_obj_create(parent);       // ← 在 c_src 上创建容器
    lv_obj_set_size(obj_2048, 480, 480);
    lv_obj_set_align(obj_2048, LV_ALIGN_CENTER);
    lv_style_init(&obj_2048_style);
    lv_style_set_pad_all(&obj_2048_style, 0);
    lv_style_set_border_width(&obj_2048_style, 0);
    lv_obj_add_style(obj_2048, &obj_2048_style, 0);
    game_2048();  
}


/*
	count_zero:统计0的个数
	返回值:
		int 返回实际统计的二维数组中0的个数
*/
int count_zero(void)
{
    int count = 0;
    for(int i=0; i<4; i++)
    {
        for(int j=0; j<4; j++)
        {
            if(arr_2048[i][j] == 0)
            {
                count++;
            }
        }
    }
    
    return count;
}

/*
	tanslation: 平移处理
	@temp_data: 一行 / 一列输出
	返回值:
		void
*/
void tanslation(int *temp_data)
{
	int index = 0; //保存每次找到非0元素存放的下标位置
    for(int i=0; i<4; i++)
    {
        if(temp_data[i] != 0)
        {
            temp_data[index] = temp_data[i];
            if(index != i)
            {
                temp_data[i] = 0;
            }
            index++;
        }
    }
}

void merge(int *temp_data)
{
	for(int i=0; i<4-1; i++)
	{
		if(temp_data[i] == temp_data[i+1])
        {
            temp_data[i] *= 2;
            temp_data[i+1] = 0;
        }
	}
}

/*
	check_win:检查是否已经成功
	返回值:
		1  成功
        0  未成功
*/
int check_win()
{
    for(int i=0; i<4; i++)
    {
        for(int j=0; j<4; j++)
        {
            if(arr_2048[i][j] >= 2048)
            {
                return 1;
            }
        }
    }
    return 0;
}

/*
	can_move:检查是否还能移动
	返回值:
		1  还能移动
        0  不能移动
*/

int can_move()
{
    if(count_zero() > 0)return 1;
    for(int i=0; i<4; i++)
    {
        for(int j=0; j<3; j++)
        {
            if(arr_2048[i][j] == arr_2048[i][j+1])
            {
                return 1;
            }
        }
    }
    for(int j=0; j<4; j++)
    {
        for(int i=0; i<3; i++)
        {
            if(arr_2048[i][j] == arr_2048[i+1][j])
            {
                return 1;
            }
        }
    }
    return 0;
}



/*
	rand_num: 在随机位置生成随机的2/4
*/
void rand_num(void)
{
	int line; //保存生成的随机行号
	int col; //保存生成到随机列号
	
	while(count_zero()) //若数组中还有空余位置
    {
        line = rand() % 4;
        col = rand() % 4;
        
        if(arr_2048[line][col] == 0) //说明生成随机位置没有数值
        {
            arr_2048[line][col] = rand() % 10 >= 4 ? 2 : 4; //生成随机的2/4
            break;
        }
    }
}

void left(void)
{
	for(int i=0; i<4; i++) //遍历每一行
	{
		tanslation(arr_2048[i]); //平移
		merge(arr_2048[i]); //合并
		tanslation(arr_2048[i]); //平移
	}
}

void right(void)
{
    int temp_data[4];
	for(int i=0; i<4; i++) //遍历每一行
	{
		for(int j=4-1; j>=0; j--)//反向获取一行数据保存在一个中间数组
        {
            temp_data[3-j] = arr_2048[i][j];
        }
        
        tanslation(temp_data); //平移
		merge(temp_data); //合并
		tanslation(temp_data); //平移
        
        for(int j=4-1; j>=0; j--)//处理完成后反向写回
        {
             arr_2048[i][j] = temp_data[3-j];
        }
	}
}

/*
	up: 上滑处理函数
	返回值:
		void
*/
void up(void)
{
	int temp_data[4] = {0};
	for(int j=0; j<4; j++) //遍历每一列
	{
		for(int i=0; i<4; i++) //读取当前列的所有行
		{
			temp_data[i] = arr_2048[i][j];
		}
        
        tanslation(temp_data); //平移
		merge(temp_data); //合并
		tanslation(temp_data); //平移
        
        for(int i=0; i<4; i++) 
		{
			arr_2048[i][j] = temp_data[i];
		}
	}
}

/*
	down: 下滑处理函数
	返回值:
		void
*/
void down(void)
{
	int temp_data[4] = {0};
	for(int j=0; j<4; j++) //遍历每一列
	{
		for(int i=4-1; i>=0; i--) //读取当前列的所有行
		{
			temp_data[3-i] = arr_2048[i][j];
		}
        
        tanslation(temp_data); //平移
		merge(temp_data); //合并
		tanslation(temp_data); //平移
        
        for(int i=4-1; i>=0; i--) //读取当前列的所有行
		{
			arr_2048[i][j] = temp_data[3-i];
		}
	}
}

void print(void)
{
    printf("------------2048_data--------------\n");
    for(int i=0; i<4; i++)
    {
        for(int j=0; j<4; j++)
        {
            printf("%d ", arr_2048[i][j]);
        }
        printf("\n");
    }
}



/*
    judge_slide_dir: 判断滑动方向
    @start_point: 起点坐标
    @end_point: 终点坐标
    返回值:
        UP 上
        DOWN 下
        LEFT 左
        RIGHT 右
*/
DIR judge_slide_dir(lv_point_t start_point, lv_point_t end_point)
{
    int dx = abs(start_point.x - end_point.x); //x轴变化区间
    int dy = abs(start_point.y - end_point.y); //y轴变化区间

    if(dx > dy) //左右方向
    {
        return start_point.x > end_point.x ? LEFT: RIGHT;
    }
    else //上下方向
    {
        return start_point.y > end_point.y ? UP: DOWN;
    }
} 


void restart()
{
    memset(arr_2048, 0, sizeof(arr_2048));
    if(game_result_label != NULL)
    {
        lv_obj_del(game_result_label);
        game_result_label = NULL;
    }
    if(obj_2048 != NULL)  // 增加空判断
    {
        lv_obj_del(obj_2048);
        obj_2048 = NULL; // 删除后置空！
    }
    rand_num();
    rand_num();
    // init_2048_ui();
    draw_2048_on_screen(c_src);
    game_active = 1;
}

void show_result_label(char *text)
{
    if(game_result_label != NULL) lv_obj_del(game_result_label);

    game_result_label = lv_label_create(lv_screen_active());
    lv_label_set_text(game_result_label, text);
    lv_obj_set_style_text_font(game_result_label, &lv_font_montserrat_48, 0);
    lv_obj_center(game_result_label);
    lv_obj_set_style_text_color(game_result_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(game_result_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(game_result_label, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(game_result_label, 20, 0);
    lv_obj_set_style_radius(game_result_label, 10, 0);

    // 可点击，绑定重启回调
    lv_obj_add_flag(game_result_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(game_result_label, restart, LV_EVENT_CLICKED, NULL);    
}

void game_2048_update_ui()
{
    char path[256] = {0};

    for(int i=0; i<4; i++)
    {
        for(int j=0; j<4; j++)
        {
            memset(path, 0, sizeof(path));
            sprintf(path, "%s/%d.bmp", PIC_SAVE_PATH, arr_2048[i][j]);
            lv_image_set_src(image_2048[i][j], path);
        }
    }
}

void game_2048_touch_cb(lv_event_t *e)
{
    //判断是否还有空位，有->继续，无->游戏结束
    if(game_active != 1)
    {
        return;
    }
    lv_point_t current_point;
    lv_event_code_t code = lv_event_get_code(e);
    //获取输入设备指针
    lv_indev_t *input_device = lv_indev_active(); 
    //获取点击屏幕坐标
    lv_indev_get_point(input_device, &current_point);
    printf("x=%d y=%d\n", current_point.x, current_point.y);

    switch(code)
    {
        case LV_EVENT_PRESSED: 
            start_point = current_point; 
            break;
        case LV_EVENT_RELEASED: 
            end_point = current_point; 
            
            DIR direction = judge_slide_dir(start_point, end_point);
            switch(direction)
            {
                case UP: up(); break;
                case DOWN: down(); break;
                case LEFT: left(); break;
                case RIGHT: right(); break;
            }
            rand_num();
            game_2048_update_ui();//重新更新界面

            //游戏结果判定，判断是否游戏已胜利
            if(check_win() == 1)
            {
                show_result_label("You Win!\nTap to restart");
                game_active = 0;
            }
            else if(can_move() == 0)
            {
                show_result_label("Game Over!\nTap to restart");
                game_active = 0;
            }
            break;
    }
}

/*
	init_2048_ui: 初始化2048界面
	返回值：
		void
*/
void init_2048_ui(void)
{
    char path[256] = {0};
    //创建一个480*480窗口显示在活动屏幕中心位置
    // obj_2048 = lv_obj_create(lv_screen_active()); 
    lv_obj_set_size(obj_2048, 480, 480);
    lv_obj_set_align(obj_2048, LV_ALIGN_CENTER); 
    //消除480 * 480 窗口内边距及边框为0
    lv_style_init(&obj_2048_style); 
    lv_style_set_pad_all(&obj_2048_style, 0); //设置内边距为0
    lv_style_set_border_width(&obj_2048_style, 0); //边框宽度为0
    lv_obj_add_style(obj_2048, &obj_2048_style, 0);

    for(int i=0; i<4; i++)
    {
        for(int j=0; j<4; j++)
        {
            memset(path, 0, sizeof(path));
            image_2048[i][j] = lv_image_create(obj_2048);
            lv_obj_set_pos(image_2048[i][j], 120*j, 120*i); 
            sprintf(path, "%s/%d.bmp", PIC_SAVE_PATH, arr_2048[i][j]);
            lv_image_set_src(image_2048[i][j], path);

            //图片控件添加一个可点击属性,因为图片默认不可以触摸，要添加该标志位就可以啦
            lv_obj_add_flag(image_2048[i][j], LV_OBJ_FLAG_CLICKABLE); 
            lv_obj_add_event_cb(image_2048[i][j], game_2048_touch_cb, LV_EVENT_PRESSED, NULL);
            lv_obj_add_event_cb(image_2048[i][j], game_2048_touch_cb, LV_EVENT_RELEASED, NULL);
        }
    }
}

void game_2048()
{ 
    rand_num();
    rand_num();
    init_2048_ui();
}