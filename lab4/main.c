#include <stdio.h>
#include "../common/common.h"

#define COLOR_BACKGROUND	FB_COLOR(0xff,0xff,0xff)

/*================ lab4: touch track drawing ================*/
/*
  需求：
  1) 每个手指轨迹颜色不同；
  2) 轨迹连贯、线宽>1；
  3) 清屏按钮，点击后清空。
  说明：不删除原有注释，新增代码尽量最小侵入。
*/

#define FINGER_MAX FINGER_NUM_MAX

/* 预设每个手指的轨迹颜色 */
static const int finger_colors[FINGER_MAX] = {
	FB_COLOR(0xff, 0x00, 0x00), /* finger0: red */
	FB_COLOR(0x00, 0x80, 0xff), /* finger1: blue */
	FB_COLOR(0x00, 0xcc, 0x00), /* finger2: green */
	FB_COLOR(0xff, 0x99, 0x00), /* finger3: orange */
	FB_COLOR(0x88, 0x00, 0xff), /* finger4: purple */
};

/* 记录手指上一次位置与是否激活 */
static int last_x[FINGER_MAX] = {0};
static int last_y[FINGER_MAX] = {0};
static unsigned char finger_active[FINGER_MAX] = {0};

/* 画刷厚度（像素） */
static int brush_size = 8; /* 可调整 6~12 之间 */

/* 清屏按钮区域 */
#define BTN_W  140
#define BTN_H   60
static int btn_x = (SCREEN_WIDTH - BTN_W - 20);
static int btn_y = 20;
static const int btn_bg = FB_COLOR(0xee,0xee,0xee);
static const int btn_border = FB_COLOR(0x66,0x66,0x66);
static const int btn_text_color = FB_COLOR(0x00,0x00,0x00);

static inline int in_rect(int x, int y, int rx, int ry, int rw, int rh){
	return (x >= rx && x < rx+rw && y >= ry && y < ry+rh);
}

static void draw_button(void){
	fb_draw_rect(btn_x, btn_y, BTN_W, BTN_H, btn_bg);
	fb_draw_border(btn_x, btn_y, BTN_W, BTN_H, btn_border);
	/* 如已初始化字体，可显示“Clear”标签；否则仅显示按钮框 */
	/* 保留注释：
	   可选：font_init("/path/to/your.ttf");
	   fb_draw_text(btn_x + 20, btn_y + 40, "Clear", 32, btn_text_color);
	*/
	/* 实际绘制按钮文字：Clear（已在 main 中初始化字体文件 font.ttc） */
	fb_draw_text(btn_x + 20, btn_y + BTN_H - 20, "Clear", 32, btn_text_color);
}

/* 画一个方形画笔点，中心在 (cx,cy) */
static void draw_brush_point(int cx, int cy, int color){
	int half = brush_size / 2;
	int rx = cx - half;
	int ry = cy - half;
	int rw = brush_size;
	int rh = brush_size;
	/* 边界裁剪 */
	if(rx < 0){ rw += rx; rx = 0; }
	if(ry < 0){ rh += ry; ry = 0; }
	if(rx + rw > SCREEN_WIDTH)  rw = SCREEN_WIDTH - rx;
	if(ry + rh > SCREEN_HEIGHT) rh = SCREEN_HEIGHT - ry;
	if(rw > 0 && rh > 0) fb_draw_rect(rx, ry, rw, rh, color);
}

/* 用简单 DDA 在两点之间打点，并用方形画笔加粗 */
static void draw_brush_line(int x1, int y1, int x2, int y2, int color){
	int dx = x2 - x1;
	int dy = y2 - y1;
	int steps = (dx>0?dx:-dx) > (dy>0?dy:-dy) ? (dx>0?dx:-dx) : (dy>0?dy:-dy);
	if(steps == 0){ draw_brush_point(x1, y1, color); return; }
	/* 为避免精度丢失，用浮点步进足够了；若需纯整数可改为 Bresenham 并在每点处画刷 */
	float fx = (float)x1, fy = (float)y1;
	float sx = (float)dx / (float)steps;
	float sy = (float)dy / (float)steps;
	for(int i=0; i<=steps; ++i){
		int px = (int)(fx + 0.5f);
		int py = (int)(fy + 0.5f);
		if(px>=0 && px<SCREEN_WIDTH && py>=0 && py<SCREEN_HEIGHT){
			draw_brush_point(px, py, color);
		}
		fx += sx; fy += sy;
	}
}

static int touch_fd;
static void touch_event_cb(int fd)
{
	int type,x,y,finger;
	type = touch_read(fd, &x,&y,&finger);
	switch(type){
	case TOUCH_PRESS:
		printf("TOUCH_PRESS：x=%d,y=%d,finger=%d\n",x,y,finger);
		if(in_rect(x,y, btn_x,btn_y, BTN_W,BTN_H)){
			/* 点击按钮：立即清屏并重画按钮 */
			fb_draw_rect(0,0,SCREEN_WIDTH,SCREEN_HEIGHT,COLOR_BACKGROUND);
			draw_button();
			/* 清空各手指状态 */
			for(int i=0;i<FINGER_MAX;++i){ finger_active[i]=0; }
			break;
		}
		if(finger>=0 && finger<FINGER_MAX){
			finger_active[finger] = 1;
			last_x[finger] = x;
			last_y[finger] = y;
			draw_brush_point(x, y, finger_colors[finger]);
		}
		break;
	case TOUCH_MOVE:
		printf("TOUCH_MOVE：x=%d,y=%d,finger=%d\n",x,y,finger);
		if(finger>=0 && finger<FINGER_MAX && finger_active[finger]){
			draw_brush_line(last_x[finger], last_y[finger], x, y, finger_colors[finger]);
			last_x[finger] = x;
			last_y[finger] = y;
		}
		break;
	case TOUCH_RELEASE:
		printf("TOUCH_RELEASE：x=%d,y=%d,finger=%d\n",x,y,finger);
		if(finger>=0 && finger<FINGER_MAX){
			finger_active[finger] = 0;
		}
		break;
	case TOUCH_ERROR:
		printf("close touch fd\n");
		close(fd);
		task_delete_file(fd);
		break;
	default:
		return;
	}
	fb_update();
	return;
}

int main(int argc, char *argv[])
{
	fb_init("/dev/fb0");
	fb_draw_rect(0,0,SCREEN_WIDTH,SCREEN_HEIGHT,COLOR_BACKGROUND);
	/* 初始化字体：优先加载运行目录下的 font.ttc（与可执行同目录 out/），
	   若需可根据设备环境改为系统字体路径。*/
	font_init("font.ttc");
	draw_button();
	fb_update();

	//打开多点触摸设备文件, 返回文件fd
	touch_fd = touch_init("/dev/input/event2");
	//添加任务, 当touch_fd文件可读时, 会自动调用touch_event_cb函数
	task_add_file(touch_fd, touch_event_cb);
	
	task_loop(); //进入任务循环
	return 0;
}
