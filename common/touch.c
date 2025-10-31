#include "common.h"

#include <stdio.h>
/* 优先使用系统头，若外部提供了 input.h 也兼容 */
#ifdef __has_include
#  if __has_include(<linux/input.h>)
#    include <linux/input.h>
#  else
#    include "input.h"
#  endif
#else
#  include "input.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

static struct finger_info{
	int x;
	int y;
	int event;
} infos[FINGER_NUM_MAX];
static int cur_slot = 0;
static int x_min = 0, x_max = 4095;
static int y_min = 0, y_max = 4095;
static int single_touch_fallback = 0; /* 兼容只有 ABS_X/ABS_Y 的设备 */

static inline int _scale_coord(int value, int min, int max, int limit)
{
	if(max <= min) return 0;
	if(value < min) value = min;
	if(value > max) value = max;
	long range = (long)max - (long)min;
	long scaled = ((long)(value - min) * (limit - 1)) / range;
	if(scaled < 0) scaled = 0;
	if(scaled >= limit) scaled = limit - 1;
	return (int)scaled;
}

static inline int adjust_x(int raw)
{
	return _scale_coord(raw, x_min, x_max, SCREEN_WIDTH);
}

static inline int adjust_y(int raw)
{
	return _scale_coord(raw, y_min, y_max, SCREEN_HEIGHT);
}

int touch_init(char *dev)
{
	int fd = open(dev, O_RDONLY);
	if(fd < 0){
		/* 失败则尝试自动扫描 /dev/input/event0..31 */
		char path[64];
		for(int i=0;i<32;++i){
			snprintf(path, sizeof(path), "/dev/input/event%d", i);
			fd = open(path, O_RDONLY);
			if(fd < 0) continue;
			/* 检查是否支持 ABS_MT 或单点 ABS_X/ABS_Y */
			struct input_absinfo tmp;
			int has_mt = (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &tmp) == 0) &&
						 (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &tmp) == 0);
			int has_st = (ioctl(fd, EVIOCGABS(ABS_X), &tmp) == 0) &&
						 (ioctl(fd, EVIOCGABS(ABS_Y), &tmp) == 0);
			if(has_mt || has_st){
				printf("touch_init: auto select %s (mt=%d st=%d)\n", path, has_mt, has_st);
				single_touch_fallback = has_mt ? 0 : 1;
				break;
			}
			close(fd); fd = -1;
		}
		if(fd < 0){
			printf("touch_init open %s error! errno = %d, and auto-detect failed\n", dev, errno);
			return -1;
		}
	}
	struct input_absinfo absinfo;
	if(ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &absinfo) == 0){
		x_min = absinfo.minimum;
		x_max = absinfo.maximum;
	}
	if(ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &absinfo) == 0){
		y_min = absinfo.minimum;
		y_max = absinfo.maximum;
	}
	/* 若为单点触摸设备，读取 ABS_X/ABS_Y 的范围 */
	if(single_touch_fallback){
		if(ioctl(fd, EVIOCGABS(ABS_X), &absinfo) == 0){
			x_min = absinfo.minimum; x_max = absinfo.maximum;
		}
		if(ioctl(fd, EVIOCGABS(ABS_Y), &absinfo) == 0){
			y_min = absinfo.minimum; y_max = absinfo.maximum;
		}
	}
	printf("touch_init: range X[%d,%d] Y[%d,%d]\n", x_min, x_max, y_min, y_max);
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
			infos[cur_slot].x = adjust_x(data.value);
			if(infos[cur_slot].event != TOUCH_PRESS) {
				infos[cur_slot].event = TOUCH_MOVE;
			}
			break;
		case ABS_MT_POSITION_Y:
			infos[cur_slot].y = adjust_y(data.value);
			if(infos[cur_slot].event != TOUCH_PRESS) {
				infos[cur_slot].event = TOUCH_MOVE;
			}
			break;
		case ABS_X: /* 单点触摸回退：无 SLOT，仅更新 slot 0 */
			if(single_touch_fallback){
				cur_slot = 0;
				infos[0].x = adjust_x(data.value);
				if(infos[0].event != TOUCH_PRESS) infos[0].event = TOUCH_MOVE;
			}
			break;
		case ABS_Y:
			if(single_touch_fallback){
				cur_slot = 0;
				infos[0].y = adjust_y(data.value);
				if(infos[0].event != TOUCH_PRESS) infos[0].event = TOUCH_MOVE;
			}
			break;
		}
		break;
	case EV_KEY:
		if(single_touch_fallback && data.code == BTN_TOUCH){
			cur_slot = 0;
			if(data.value){
				infos[0].event = TOUCH_PRESS;
			}else{
				*x = infos[0].x; *y = infos[0].y; *finger = 0;
				infos[0].event = TOUCH_NO_EVENT;
				return TOUCH_RELEASE;
			}
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
