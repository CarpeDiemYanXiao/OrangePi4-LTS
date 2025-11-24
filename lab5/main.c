#include <stdio.h>
#include "../common/common.h"

#define COLOR_BACKGROUND	FB_COLOR(0xff,0xff,0xff)
#define COLOR_TEXT			FB_COLOR(0x0,0x0,0x0)

static int touch_fd;
static int bluetooth_fd;

#define TIME_X	(SCREEN_WIDTH-160)
#define TIME_Y	0
#define TIME_W	100
#define TIME_H	30

#define SEND_X	(SCREEN_WIDTH-60)
#define SEND_Y	0
#define SEND_W	60
#define SEND_H	60

static void touch_event_cb(int fd)
{
	int type,x,y,finger;
	type = touch_read(fd, &x,&y,&finger);
	switch(type){
	case TOUCH_PRESS:
		//printf("type=%d,x=%d,y=%d,finger=%d\n",type,x,y,finger);
		if((x>=SEND_X)&&(x<SEND_X+SEND_W)&&(y>=SEND_Y)&&(y<SEND_Y+SEND_H)) {
			/* 发送指定消息（包含多行中文）到 RFCOMM - 使用 CRLF 换行以兼容终端 */
			char msg[] = "you are a good man\r\nI am a Huster\r\n70\r\n生日快乐\r\n华中科技大学\r\n华中科技大学七十周岁生日快乐\r\n";
			int len = (int)(sizeof(msg) - 1);
			printf("bluetooth tty send message (%d bytes)\n", len);
			myWrite_nonblock(bluetooth_fd, msg, len);
		}
		break;
	case TOUCH_ERROR:
		printf("close touch fd\n");
		task_delete_file(fd);
		close(fd);
		break;
	default:
		return;
	}
	fb_update();
	return;
}

static int pen_y=30;
static void bluetooth_tty_event_cb(int fd)
{
	char buf[128];
	int n;

	n = myRead_nonblock(fd, buf, sizeof(buf)-1);
	if(n <= 0) {
		printf("close bluetooth tty fd\n");
		//task_delete_file(fd);
		//close(fd);
		exit(0);
		return;
	}

	buf[n] = '\0';
	printf("bluetooth tty receive \"%s\"\n", buf);
	/* 将收到的数据按 '\r' 或 '\n' 分行显示到屏幕上 */
	int start = 0;
	for (int i = 0; i < n; ++i) {
		if (buf[i] == '\r' || buf[i] == '\n') {
			int len_line = i - start;
			if (len_line > 0) {
				char line[128];
				if (len_line >= (int)sizeof(line)) len_line = (int)sizeof(line) - 1;
				memcpy(line, buf + start, len_line);
				line[len_line] = '\0';
				fb_draw_text(2, pen_y, line, 24, COLOR_TEXT);
				pen_y += 30;
			}
			/* 跳过连续的 CR/LF */
			start = i + 1;
			while (start < n && (buf[start] == '\r' || buf[start] == '\n')) start++;
			i = start - 1;
		}
	}
	/* 处理尾部没有换行的最后一行 */
	if (start < n) {
		int len_line = n - start;
		char line[128];
		if (len_line >= (int)sizeof(line)) len_line = (int)sizeof(line) - 1;
		memcpy(line, buf + start, len_line);
		line[len_line] = '\0';
		fb_draw_text(2, pen_y, line, 24, COLOR_TEXT);
		pen_y += 30;
	}
	fb_update();
	return;
}

static int bluetooth_tty_init(const char *dev)
{
	int fd = open(dev, O_RDWR|O_NOCTTY|O_NONBLOCK); /*非阻塞模式*/
	if(fd < 0){
		printf("bluetooth_tty_init open %s error(%d): %s\n", dev, errno, strerror(errno));
		return -1;
	}
	return fd;
}

static int st=0;
static void timer_cb(int period) /*该函数0.5秒执行一次*/
{
	char buf[100];
	sprintf(buf, "%d", st++);
	fb_draw_rect(TIME_X, TIME_Y, TIME_W, TIME_H, COLOR_BACKGROUND);
	fb_draw_border(TIME_X, TIME_Y, TIME_W, TIME_H, COLOR_TEXT);
	fb_draw_text(TIME_X+2, TIME_Y+20, buf, 24, COLOR_TEXT);
	fb_update();
	return;
}

int main(int argc, char *argv[])
{
	fb_init("/dev/fb0");
	font_init("./font.ttc");
	fb_draw_rect(0,0,SCREEN_WIDTH,SCREEN_HEIGHT,COLOR_BACKGROUND);
	fb_draw_border(SEND_X, SEND_Y, SEND_W, SEND_H, COLOR_TEXT);
	fb_draw_text(SEND_X+2, SEND_Y+30, "send", 24, COLOR_TEXT);
	fb_update();

	touch_fd = touch_init("/dev/input/event0");
	task_add_file(touch_fd, touch_event_cb);

	bluetooth_fd = bluetooth_tty_init("/dev/rfcomm0");
	if(bluetooth_fd == -1) return 0;
	task_add_file(bluetooth_fd, bluetooth_tty_event_cb);

	task_add_timer(500, timer_cb); /*增加0.5秒的定时器*/
	task_loop();
	return 0;
}
