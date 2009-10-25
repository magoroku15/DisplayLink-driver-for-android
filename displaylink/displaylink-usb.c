#ifndef DISPLAYLINK
        #include "displaylink.h"
#endif

/* taken from libdlo */
static uint16_t lfsr16(uint16_t v)
{
  uint32_t _v   = 0xFFFF;

	v = cpu_to_le16(v);

  while (v--) {
    _v = ((_v << 1) | (((_v >> 15) ^ (_v >> 4) ^ (_v >> 2) ^ (_v >> 1)) & 1)) & 0xFFFF;
  }
  return (uint16_t) _v;
}



/* displaylink functions */
void displaylink_bulk_callback(struct urb *urb)
{

	struct displaylink_dev *dev = urb->context;
	complete(&dev->done);
	//printk("displaylink BULK transfer complete %d\n", urb->actual_length);

}

void displaylink_edid(struct displaylink_dev *dev)
{
	int i;
	int ret;
	char rbuf[2];

	for (i = 0; i < 128; i++) {
                ret =
                    usb_control_msg(dev->udev,
                                    usb_rcvctrlpipe(dev->udev, 0), (0x02),
                                    (0x80 | (0x02 << 5)), i << 8, 0xA1, rbuf, 2,
                                    0);
                //printk("ret control msg edid %d: %d [%d]\n",i, ret, rbuf[1]);
                dev->edid[i] = rbuf[1];
        }

}

void displaylink_get_best_edid(struct displaylink_dev *dev) {
	return;
}

static char *displaylink_set_register_16(char *bufptr, uint8_t reg, uint16_t val)
{

	bufptr = displaylink_set_register(bufptr, reg, val >> 8);
	bufptr = displaylink_set_register(bufptr, reg+1, val & 0xFF);

	return bufptr;
}

static char *displaylink_set_register_le16(char *bufptr, uint8_t reg, uint16_t val)
{

	bufptr = displaylink_set_register(bufptr, reg, val & 0xFF);
	bufptr = displaylink_set_register(bufptr, reg+1, val >> 8);

	return bufptr;
}

char *displaylink_edid_to_reg(struct detailed_timing *edid, char *bufptr, int width, int height, int freq) {

	uint16_t edid_w  ;
	uint16_t edid_h ;
	uint16_t edid_x_ds ;
	uint16_t edid_x_de ;
	uint16_t edid_y_ds ;
	uint16_t edid_y_de ;
	uint16_t edid_x_ec ;
	uint16_t edid_h_se ;
	uint16_t edid_y_ec ;
	uint16_t edid_v_se ;
	uint16_t edid_pclock ;

	/* display width */
	edid_w = EDID_GET_WIDTH(edid) ;
	if (width!= 0) {
		edid_w = width;	
	}

	/* display height */
	edid_h  = EDID_GET_HEIGHT(edid) ;
	if (height!= 0) {
		edid_h = height;	
	}

	/* display x start/end */
	edid_x_ds = (EDID_GET_HBLANK(edid) - EDID_GET_HSYNC(edid)) ;
	edid_x_de = (edid_x_ds + edid_w) ;

	/* display y start/end */
	edid_y_ds = (EDID_GET_VBLANK(edid) - EDID_GET_VSYNC(edid));
	edid_y_de = (edid_y_ds + edid_h);

	/* x end count */
	edid_x_ec = (edid_w + EDID_GET_HBLANK(edid) - 1);
	edid_h_se = (EDID_GET_HPULSE(edid) + 1) ;

	/* y end count */
	edid_y_ec = (edid_h + EDID_GET_VBLANK(edid)) ;
	edid_v_se = (EDID_GET_VPULSE(edid)) ;


	/* pixel clock */
	edid_pclock = edid->pixel_clock*2;

	if (freq != 0) {
		/* calc new pixel clock based on freq */
	}


	bufptr = displaylink_set_register_16(bufptr, 0x01, lfsr16(edid_x_ds)) ;
	bufptr = displaylink_set_register_16(bufptr, 0x03, lfsr16(edid_x_de)) ;
	bufptr = displaylink_set_register_16(bufptr, 0x05, lfsr16(edid_y_ds)) ;
	bufptr = displaylink_set_register_16(bufptr, 0x07, lfsr16(edid_y_de)) ;

	bufptr = displaylink_set_register_16(bufptr, 0x09, lfsr16(edid_x_ec)) ;

	bufptr = displaylink_set_register_16(bufptr, 0x0B, lfsr16(1)) ;
	bufptr = displaylink_set_register_16(bufptr, 0x0D, lfsr16(edid_h_se)) ;

	bufptr = displaylink_set_register_16(bufptr, 0x0F, edid_w) ;

	bufptr = displaylink_set_register_16(bufptr, 0x11, lfsr16(edid_y_ec)) ;

	bufptr = displaylink_set_register_16(bufptr, 0x13, lfsr16(0)) ;
	bufptr = displaylink_set_register_16(bufptr, 0x15, lfsr16(edid_v_se)) ;

	bufptr = displaylink_set_register_16(bufptr, 0x17, edid_h) ;

	bufptr = displaylink_set_register_le16(bufptr, 0x1B, edid_pclock) ;

	return bufptr;
}

int displaylink_bulk_msg(struct displaylink_dev *dev, int len)
{

	int ret;

	init_completion(&dev->done);

	dev->tx_urb->actual_length = 0;
	dev->tx_urb->transfer_buffer_length = len;

	ret = usb_submit_urb(dev->tx_urb, GFP_KERNEL);
	if (!wait_for_completion_timeout(&dev->done, 1000)) {
		usb_kill_urb(dev->tx_urb);
		printk("usb timeout !!!\n");
	}

	return dev->tx_urb->actual_length;

}


char *displaylink_set_register(char *bufptr, uint8_t reg, uint8_t val)
{

	*bufptr++ = 0xAF;
	*bufptr++ = 0x20;
	*bufptr++ = reg;
	*bufptr++ = val;

	return bufptr;

}

int displaylink_set_video_mode(struct displaylink_dev *dev, int mode, int width, int height, int freq)
{

	char *bufptr;
	int ret;

	struct edid *edid = (struct edid *) dev->edid;
	struct detailed_timing *best_edid =  &edid->detailed_timings[mode];	

	if (dev->udev == NULL)
		return 0;

	dev->base16 = 0;
	dev->base8 = dev->screen_size;

	bufptr = dev->buf;

	mutex_lock(&dev->bulk_mutex);

	// set registers
	bufptr = displaylink_set_register(bufptr, 0xFF, 0x00);

	// set color depth
	bufptr = displaylink_set_register(bufptr, 0x00, 0x00);

	// set addresses
	bufptr =
		displaylink_set_register(bufptr, 0x20,
		(char)(dev->base16 >> 16));
	bufptr =
		displaylink_set_register(bufptr, 0x21,
		(char)(dev->base16 >> 8));
	bufptr =
		displaylink_set_register(bufptr, 0x22,
		(char)(dev->base16));

	bufptr =
		displaylink_set_register(bufptr, 0x26,
		(char)(dev->base8 >> 16));

	bufptr =
		displaylink_set_register(bufptr, 0x27,
		(char)(dev->base8 >> 8));

	bufptr =
		displaylink_set_register(bufptr, 0x28,
		(char)(dev->base8));

	if (width != 0) {
		printk("displaylink setting resolution to %dx%d\n", width, height);
	}
	
	bufptr = displaylink_edid_to_reg(best_edid, bufptr, width, height, freq);


	// blank
	bufptr = displaylink_set_register(bufptr, 0x1F, 0x00);

	// end registers
	bufptr = displaylink_set_register(bufptr, 0xFF, 0xFF);

	// send
	ret = displaylink_bulk_msg(dev, bufptr - dev->buf);
	printk("set video mode bulk message: %d %d\n", ret,
			       bufptr - dev->buf);
	// flush
	ret = displaylink_bulk_msg(dev, 0);
	printk("displaylink register flush: %d\n", ret);

	if (width == 0) {
		dev->line_length = EDID_GET_WIDTH(best_edid) * (FB_BPP/8) ;
	}
	else {
		dev->line_length = width * (FB_BPP/8) ;
	}

	mutex_unlock(&dev->bulk_mutex);

	return 0;

}

int displaylink_setup(struct displaylink_dev *dev) {

	int ret;
	unsigned char buf[4];

	struct edid *edid = (struct edid *) dev->edid;

	struct detailed_timing *best_edid =  &edid->detailed_timings[0] ;

	ret = usb_control_msg(dev->udev,
                                    usb_rcvctrlpipe(dev->udev, 0), (0x02),
                                    (0x80 | (0x02 << 5)), 0, 0, buf, 4,
                                    5000);

	if (ret != 4) {
		return -1 ;
	}

	switch ((buf[3] >> 4) & 0xF) {
		case DL_CHIP_TYPE_BASE:
			strcpy(dev->chiptype, "base");
			break;
		case DL_CHIP_TYPE_ALEX:
			strcpy(dev->chiptype, "alex");
			break;
		case DL_CHIP_TYPE_OLLIE:
			strcpy(dev->chiptype, "ollie");
			break;
		default:
			if (buf[3] == DL_CHIP_TYPE_OLLIE)
				strcpy(dev->chiptype, "ollie");
			else
				strcpy(dev->chiptype, "unknown");
	}

	printk("found DisplayLink Chip %s\n", dev->chiptype);

	// set encryption key (null)
        memcpy(dev->buf, STD_CHANNEL, 16);
        ret = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0),
		0x12, (0x02 << 5), 0, 0,
		dev->buf, 16, 0);

	printk("sent encryption null key: %d\n", ret);

	dev->screen_size = 800 * 480 * 2 * (FB_BPP / 8);	// hideo

        dev->line_length = EDID_GET_WIDTH(best_edid) * (FB_BPP / 8);

	printk("displaylink monitor info: W(%d) H(%d) clock(%d)\n",
		EDID_GET_WIDTH(best_edid),
		EDID_GET_HEIGHT(best_edid),
		best_edid->pixel_clock
		);

	

	return 0;
}

int
displaylink_draw_rect(struct displaylink_dev *dev, int x, int y, int width, int height,
          unsigned char red, unsigned char green, unsigned char blue)
{

        int i, j, base;
        int ret;
        unsigned short col =
            (((((red) & 0xF8) | ((green) >> 5)) & 0xFF) << 8) +
            (((((green) & 0x1C) << 3) | ((blue) >> 3)) & 0xFF);
        int rem = width;

        char *bufptr;

	if (dev->udev == NULL)
		return -EINVAL;

        if (x + width > dev->fb_info->var.xres)
                return -EINVAL;

        if (y + height > dev->fb_info->var.yres * 2)	// hideo
                return -EINVAL;

        mutex_lock(&dev->bulk_mutex);

        base = dev->base16 + (dev->line_length * y) + (x * 2);

        bufptr = dev->buf;

        for (i = y; i < y + height; i++) {

                for (j = 0; j < width * 2; j += 2) {
                        dev->backing_buffer[base - dev->base16 + j] = (char)(col >> 8);
                        dev->backing_buffer[base - dev->base16 + j + 1] = (char)(col);
                }
                if (dev->bufend - bufptr < BUF_HIGH_WATER_MARK) {
                        ret = displaylink_bulk_msg(dev, bufptr - dev->buf);
                        bufptr = dev->buf;
                }

                rem = width;

                while (rem) {

                        if (dev->bufend - bufptr < BUF_HIGH_WATER_MARK) {
                                ret =
                                    displaylink_bulk_msg(dev,
                                                  bufptr - dev->buf);
                                bufptr = dev->buf;
                        }

                        *bufptr++ = 0xAF;
                        *bufptr++ = 0x69;

                        *bufptr++ = (char)(base >> 16);
                        *bufptr++ = (char)(base >> 8);
                        *bufptr++ = (char)(base);

                        if (rem > 255) {
                                *bufptr++ = 255;
                                *bufptr++ = 255;
                                rem -= 255;
                                base += 255 * 2;
                        } else {
                                *bufptr++ = rem;
                                *bufptr++ = rem;
                                base += rem * 2;
                                rem = 0;
                        }

                        *bufptr++ = (char)(col >> 8);
                        *bufptr++ = (char)(col);

                }

                base += (dev->line_length) - (width * 2);

        }

        if (bufptr > dev->buf)
                ret = displaylink_bulk_msg(dev, bufptr - dev->buf);

        mutex_unlock(&dev->bulk_mutex);

        return 1;
}


int
displaylink_copyarea(struct displaylink_dev *dev, int dx, int dy, int sx, int sy,
         int width, int height)
{
        int base;
        int source;
        int rem;
        int i, ret;

        char *bufptr;

	
	if (dev->udev == NULL)
		return -EINVAL;

        if (dx + width > dev->fb_info->var.xres)
                return -EINVAL;

        if (dy + height > dev->fb_info->var.yres * 2)			// hideo
                return -EINVAL;

        mutex_lock(&dev->bulk_mutex);

        base =
            dev->base16 + (dev->line_length * dy) + (dx * 2);
        source = (dev->line_length * sy) + (sx * 2);

        bufptr = dev->buf;

        for (i = sy; i < sy + height; i++) {

                memcpy(dev->backing_buffer + base - dev->base16,
                       dev->backing_buffer + source, width * 2);

                if (dev->bufend - bufptr < BUF_HIGH_WATER_MARK) {
                        ret = displaylink_bulk_msg(dev, bufptr - dev->buf);
                        bufptr = dev->buf;
                }

                rem = width;

                while (rem) {

                        if (dev->bufend - bufptr < BUF_HIGH_WATER_MARK) {
                                ret =
                                    displaylink_bulk_msg(dev,
                                                  bufptr - dev->buf);
                                bufptr = dev->buf;
                        }

                        *bufptr++ = 0xAF;
                        *bufptr++ = 0x6A;

                        *bufptr++ = (char)(base >> 16);
                        *bufptr++ = (char)(base >> 8);
                        *bufptr++ = (char)(base);

                        if (rem > 255) {
                                *bufptr++ = 255;
                                *bufptr++ = (char)(source >> 16);
                                *bufptr++ = (char)(source >> 8);
                                *bufptr++ = (char)(source);

                                rem -= 255;
                                base += 255 * 2;
                                source += 255 * 2;

                        } else {
                                *bufptr++ = rem;
                                *bufptr++ = (char)(source >> 16);
                                *bufptr++ = (char)(source >> 8);
                                *bufptr++ = (char)(source);

                                base += rem * 2;
                                source += rem * 2;
                                rem = 0;
                        }
                }

                base += (dev->line_length) - (width * 2);
                source += (dev->line_length) - (width * 2);
        }

        if (bufptr > dev->buf)
                ret = displaylink_bulk_msg(dev, bufptr - dev->buf);

        mutex_unlock(&dev->bulk_mutex);

        return 1;
}

int displaylink_blank(struct displaylink_dev *dev, int blankmode){
	return 0;
}
