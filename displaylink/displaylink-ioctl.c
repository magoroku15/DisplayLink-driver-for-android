
#ifndef DISPLAYLINK
        #include "displaylink.h"
#endif


/* ioctl structure */
struct dloarea {
        int x, y;
        int w, h;
        int x2, y2;
};

struct dlores {
	int w,h;
	int freq;
};


/* ioctl functions */

int displaylink_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{

	struct displaylink_dev *dev = info->par;
	struct dloarea *area = NULL;
	struct dlores *res = NULL;
	char *name;
	
	if (dev->udev == NULL) {
		return -EINVAL;
	}
	printk("displaylink_ioctl: %x\n", cmd);
	if (cmd == 0xAD) {
		char *edid = (char *)arg;
		displaylink_edid(dev);
		if (copy_to_user(edid, dev->edid, 128)) {
			return -EFAULT;
		}
		return 0;
	}

	if (cmd == 0xAA || cmd == 0xAB || cmd == 0xAC) {

		area = (struct dloarea *)arg;


		if (area->x < 0)
			area->x = 0;

		if (area->x > info->var.xres)
			area->x = info->var.xres;

		if (area->y < 0)
			area->y = 0;

		if (area->y > info->var.yres)
			area->y = info->var.yres;
	}

	if (cmd == 0xAA) {
		displaylink_image_blit(dev, area->x, area->y, area->w, area->h,
			   info->screen_base);
	}
	else if (cmd == 0xAB) {

		if (area->x2 < 0)
                        area->x2 = 0;

		if (area->y2 < 0)
                        area->y2 = 0;

		displaylink_copyarea(dev,
			area->x2, area->y2, area->x, area->y, area->w, area->h);
	}
	else if (cmd == 0xAE) {
		res = (struct dlores *) arg;
		displaylink_set_video_mode(dev,0, res->w, res->h, res->freq);

	}
	else if (cmd == 0xAF) {
		name = (char *) arg;
		if (copy_to_user(name, dev->name, 64)) {
			return -EFAULT;
		}
		return 0;
	}
	else if (cmd == 0xB0) {
		name = (char *) arg;
		if (copy_to_user(name, "displaylink", 11)) {
			return -EFAULT;
		}
		return 0;
	}

	return 0;
}
