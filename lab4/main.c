/* lab4: 多点触摸轨迹 + 清屏按钮
   要求：
   1) 不同手指不同颜色；
   2) 轨迹连贯（MOVE 时连接 PRESS/上次坐标）；
   3) 轨迹要比较粗（线宽>1像素）；
   4) 右上角提供“清屏按钮”，点击清除屏幕内容。 */

#include <stdio.h>
#include "../common/common.h"

#define COLOR_BACKGROUND   FB_COLOR(235,235,235)
#define COLOR_BTN_BG       FB_COLOR(40,40,40)
#define COLOR_BTN_BORDER   FB_COLOR(220,220,220)
#define COLOR_BTN_TEXT     FB_COLOR(255,255,255)

/* 清屏按钮区域（右上角） */
#define BTN_W 140
#define BTN_H 60
#define BTN_X (SCREEN_WIDTH - BTN_W - 16)
#define BTN_Y 16

/* 轨迹线宽（像素） */
#define STROKE 10

static int touch_fd;
static int last_x[FINGER_NUM_MAX];
static int last_y[FINGER_NUM_MAX];
static int active[FINGER_NUM_MAX];

static int finger_color[FINGER_NUM_MAX] = {
	FB_COLOR(255, 80, 80),   /* finger 0: 红 */
	FB_COLOR(80, 255, 120),  /* finger 1: 绿 */
	FB_COLOR(80, 160, 255),  /* finger 2: 蓝 */
	FB_COLOR(255, 200, 80),  /* finger 3: 橙 */
	FB_COLOR(200, 120, 255)  /* finger 4: 紫 */
};

static inline int in_button(int x, int y)
{
	return (x >= BTN_X && x < BTN_X + BTN_W && y >= BTN_Y && y < BTN_Y + BTN_H);
}

static inline int clamp_coord(int value, int max)
{
	if(value < 0) return 0;
	if(value >= max) return max - 1;
	return value;
}

static void draw_button(void)
{
	fb_draw_rect(BTN_X, BTN_Y, BTN_W, BTN_H, COLOR_BTN_BG);
	fb_draw_border(BTN_X, BTN_Y, BTN_W, BTN_H, COLOR_BTN_BORDER);
	/* 在按钮上绘制 CLEAR 文本；尝试多条常见字体路径，找一条可用的 */
	static int font_ready = 0;
	if(!font_ready){
		const char* candidates[] = {
			"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
			"/usr/share/fonts/truetype/freefont/FreeSans.ttf",
			"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
			"/usr/share/fonts/truetype/arphic/ukai.ttc",
			"/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
		};
		for(unsigned i=0;i<sizeof(candidates)/sizeof(candidates[0]);++i){
			font_init((char*)candidates[i]);
			/* fb_read_font_image 会在未初始化或失败时输出提示，这里只尝试加载，不强制校验 */
			font_ready = 1; /* 先置位，失败时后续绘制会安全返回 */
			break;
		}
	}
	fb_draw_text(BTN_X + 22, BTN_Y + BTN_H - 18, "CLEAR", 28, COLOR_BTN_TEXT);
}

/* 用“加粗画点”的方式实现粗线：在直线每个像素点处画一个 STROKE×STROKE 的小块 */
static void draw_thick_line(int x1, int y1, int x2, int y2, int color)
{
	int dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
	int dy = (y2 > y1) ? (y2 - y1) : (y1 - y2);
	int sx = (x1 < x2) ? 1 : -1;
	int sy = (y1 < y2) ? 1 : -1;
	int err = dx - dy;
	int x = x1, y = y1;
	int half = STROKE / 2;

	for(;;){
		fb_draw_rect(x - half, y - half, STROKE, STROKE, color);
		if(x == x2 && y == y2) break;
		int e2 = err << 1;
		if(e2 > -dy){ err -= dy; x += sx; }
		if(e2 <  dx){ err += dx; y += sy; }
	}
}

static void clear_screen_and_state(void)
{
	for(int i=0;i<FINGER_NUM_MAX;++i) active[i] = 0;
	fb_draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BACKGROUND);
	draw_button();
	fb_update();
}

static void touch_event_cb(int fd)
{
	int type,x,y,finger;
	type = touch_read(fd, &x,&y,&finger);
	switch(type){
	case TOUCH_PRESS:
		x = clamp_coord(x, SCREEN_WIDTH);
		y = clamp_coord(y, SCREEN_HEIGHT);
		/* 保留原有打印 */
		printf("TOUCH_PRESS：x=%d,y=%d,finger=%d\n",x,y,finger);
		if(in_button(x, y)){
			clear_screen_and_state();
			return;
		}
		if(finger >=0 && finger < FINGER_NUM_MAX){
			last_x[finger] = x; last_y[finger] = y; active[finger] = 1;
			fb_draw_rect(x - STROKE/2, y - STROKE/2, STROKE, STROKE, finger_color[finger]);
		}
		break;
	case TOUCH_MOVE:
		x = clamp_coord(x, SCREEN_WIDTH);
		y = clamp_coord(y, SCREEN_HEIGHT);
		/* 保留原有打印 */
		printf("TOUCH_MOVE：x=%d,y=%d,finger=%d\n",x,y,finger);
		if(finger >=0 && finger < FINGER_NUM_MAX && active[finger]){
			draw_thick_line(last_x[finger], last_y[finger], x, y, finger_color[finger]);
			last_x[finger] = x; last_y[finger] = y;
		}
		break;
	case TOUCH_RELEASE:
		x = clamp_coord(x, SCREEN_WIDTH);
		y = clamp_coord(y, SCREEN_HEIGHT);
		/* 保留原有打印 */
		printf("TOUCH_RELEASE：x=%d,y=%d,finger=%d\n",x,y,finger);
		if(finger >=0 && finger < FINGER_NUM_MAX) active[finger] = 0;
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
}

int main(int argc, char *argv[])
{
	fb_init("/dev/fb0");
	fb_draw_rect(0,0,SCREEN_WIDTH,SCREEN_HEIGHT,COLOR_BACKGROUND);
	draw_button();
	fb_update();

	// 打开多点触摸设备文件, 可以通过命令行参数指定；未指定则尝试 /dev/input/event2 或自动扫描
	const char *dev = (argc > 1 && argv[1]) ? argv[1] : "/dev/input/event2";
	printf("try open input device: %s\n", dev);
	touch_fd = touch_init((char*)dev);
	//添加任务, 当touch_fd文件可读时, 会自动调用touch_event_cb函数
	task_add_file(touch_fd, touch_event_cb);
    
	task_loop(); //进入任务循环
	return 0;
}
