#include "common.h"

#include <stdio.h>
//#include <linux/input.h>
#include "input.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static struct finger_info{
	int x;
	int y;
	int event;
} infos[FINGER_NUM_MAX];
static int cur_slot = 0;

/* 运行时从设备查询的坐标范围，作为缩放依据（默认按 0~4095） */
static int abs_min_x = 0, abs_max_x = 4095;
static int abs_min_y = 0, abs_max_y = 4095;

static inline int _scale_to_screen(int v, int vmin, int vmax, int out)
{
	if(vmax <= vmin) return 0;
	long long num = (long long)(v - vmin) * (long long)(out - 1);
	long long den = (long long)(vmax - vmin);
	long long t = num / den;
	if(t < 0) t = 0;
	if(t >= out) t = out - 1;
	return (int)t;
}

static inline int ADJUST_X_FUNC(int n)
{ return _scale_to_screen(n, abs_min_x, abs_max_x, SCREEN_WIDTH); }
static inline int ADJUST_Y_FUNC(int n)
{ return _scale_to_screen(n, abs_min_y, abs_max_y, SCREEN_HEIGHT); }

int touch_init(char *dev)
{
	int fd = open(dev, O_RDONLY);
	if(fd < 0){
		printf("touch_init open %s error!errno = %d\n", dev, errno);
		return -1;
	}

	/* 查询设备坐标范围，兼容不同面板（例如 0~32767） */
	struct input_absinfo ainfo;
	if(ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ainfo) == 0){
		abs_min_x = ainfo.minimum;
		abs_max_x = ainfo.maximum;
	}
	if(ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ainfo) == 0){
		abs_min_y = ainfo.minimum;
		abs_max_y = ainfo.maximum;
	}
	printf("touch abs range: X=[%d,%d], Y=[%d,%d]\n", abs_min_x, abs_max_x, abs_min_y, abs_max_y);
	return fd;
}

/*return:
	TOUCH_NO_EVENT
	TOUCH_PRESS
	TOUCH_MOVE
	TOUCH_RELEASE
	TOUCH_ERROR
	x: [0 ~ SCREEN_WIDTH)
	y: [0 ~ SCREEN_HEIGHT)
	finger: 0,1,2,3,4
*/

/* 坐标缩放：根据运行时查询到的 min/max 映射到屏幕像素 */

int touch_read(int touch_fd, int *x, int *y, int *finger)
{
	struct input_event data;
	int n, ret;
	if((n = read(touch_fd, &data, sizeof(data))) != sizeof(data)){
		printf("touch_read error %d, errno=%d\n", n, errno);
		return TOUCH_ERROR;
	}
//	printf("event read: type-code-value = %d-%d-%d\n", data.type, data.code, data.value);
	switch(data.type)
	{
	case EV_ABS:
		switch(data.code)
		{
		case ABS_MT_SLOT:
			if(data.value >= 0 && data.value < FINGER_NUM_MAX) {
				int old = cur_slot;
				cur_slot = data.value;
				if(infos[old].event != TOUCH_NO_EVENT) {
					*x = infos[old].x;
					*y = infos[old].y;
					*finger = old;
					ret = infos[old].event;
					infos[old].event = TOUCH_NO_EVENT;
					return ret;
				}
			}
			break;
		case ABS_MT_TRACKING_ID:
			if(data.value == -1){
				*x = infos[cur_slot].x;
				*y = infos[cur_slot].y;
				*finger = cur_slot;
				infos[cur_slot].event = TOUCH_NO_EVENT;
				return TOUCH_RELEASE;
			}
			else{
				infos[cur_slot].event = TOUCH_PRESS;
			}
			break;
		case ABS_MT_POSITION_X:
			infos[cur_slot].x = ADJUST_X_FUNC(data.value);
			if(infos[cur_slot].event != TOUCH_PRESS) {
				infos[cur_slot].event = TOUCH_MOVE;
			}
			break;
		case ABS_MT_POSITION_Y:
			infos[cur_slot].y = ADJUST_Y_FUNC(data.value);
			if(infos[cur_slot].event != TOUCH_PRESS) {
				infos[cur_slot].event = TOUCH_MOVE;
			}
			break;
		}
		break;
	case EV_SYN:
		switch(data.code)
		{
		case SYN_REPORT:
			if(infos[cur_slot].event != TOUCH_NO_EVENT){
				*x = infos[cur_slot].x;
				*y = infos[cur_slot].y;
				*finger = cur_slot;
				ret = infos[cur_slot].event;
				infos[cur_slot].event = TOUCH_NO_EVENT;
				return ret;
			}
			break;
		}
		break;
	}
	return TOUCH_NO_EVENT;
}
