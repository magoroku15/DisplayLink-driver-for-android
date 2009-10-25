#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30)
#include <drm/drm_edid.h>
#else
#include "drm_edid.h"
#endif

// as libdlo
#define BUF_HIGH_WATER_MARK 1024
#define BUF_SIZE 64*1024

#define DISPLAYLINK 1

#define FB_BPP 16

#define STD_CHANNEL        "\x57\xCD\xDC\xA7\x1C\x88\x5E\x15\x60\xFE\xC6\x97\x16\x3D\x47\xF2"

#define DL_CHIP_TYPE_BASE 0xB
#define DL_CHIP_TYPE_ALEX 0xF
#define DL_CHIP_TYPE_OLLIE 0xF1


#define EDID_GET_WIDTH(edid) ( ( ( (uint16_t) edid->data.pixel_data.hactive_hi ) << 8 ) | (uint16_t) edid->data.pixel_data.hactive_lo )
#define EDID_GET_HEIGHT(edid) ( ( ( (uint16_t) edid->data.pixel_data.vactive_hi ) << 8 ) | (uint16_t) edid->data.pixel_data.vactive_lo )

#define EDID_GET_HBLANK(edid) ( ( ( (uint16_t) edid->data.pixel_data.hblank_hi ) << 8 ) | (uint16_t) edid->data.pixel_data.hblank_lo )
#define EDID_GET_VBLANK(edid) ( ( ( (uint16_t) edid->data.pixel_data.vblank_hi ) << 8 ) | (uint16_t) edid->data.pixel_data.vblank_lo )

#define EDID_GET_HSYNC(edid) ( ( ( (uint16_t) edid->data.pixel_data.hsync_offset_hi ) << 8 ) | (uint16_t) edid->data.pixel_data.hsync_offset_lo )
#define EDID_GET_VSYNC(edid) ( ( ( (uint16_t) edid->data.pixel_data.vsync_offset_hi ) << 8 ) | (uint16_t) edid->data.pixel_data.vsync_offset_lo )

#define EDID_GET_HPULSE(edid) ( ( ( (uint16_t) edid->data.pixel_data.hsync_pulse_width_hi ) << 8 ) | (uint16_t) edid->data.pixel_data.hsync_pulse_width_lo )
#define EDID_GET_VPULSE(edid) ( ( ( (uint16_t) edid->data.pixel_data.vsync_pulse_width_hi ) << 8 ) | (uint16_t) edid->data.pixel_data.vsync_pulse_width_lo )

struct displaylink_dev {
	atomic_t fb_count;
        struct usb_device *udev;
        struct mutex fb_mutex;
        struct fb_info *fb_info;
        int screen_size;
        int line_length;
        struct usb_interface *interface;
        struct urb *tx_urb, *ctrl_urb;
        struct usb_ctrlrequest dr;
        char *buf;
        char *bufend;
        char *backing_buffer;
        struct mutex bulk_mutex;
        char edid[128];
	char chiptype[8];
	char name[64];
        struct completion done;
        int base16;
        int base16d;
        int base8;
        int base8d;
};


struct displaylink_orphaned_dev {
	atomic_t fb_count;
        struct usb_device *udev;
        struct mutex fb_mutex;
        struct fb_info *fb_info;
        int screen_size;
        int line_length;
};

struct dlfb_video_mode {

        uint8_t col;
        uint32_t hclock;
        uint32_t vclock;
        uint8_t unknown1[6];
        uint16_t xres;
        uint8_t unknown2[6];
        uint16_t yres;
        uint8_t unknown3[4];

} __attribute__ ((__packed__));


void displaylink_bulk_callback(struct urb *urb);
int displaylink_bulk_msg(struct displaylink_dev *dev, int len);


int displaylink_activate_framebuffer(struct displaylink_dev *dev, int mode);
int displaylink_setup(struct displaylink_dev *dev);
int displaylink_set_video_mode(struct displaylink_dev *dev, int mode, int width, int height, int freq);
void displaylink_get_best_edid(struct displaylink_dev *dev);
int
displaylink_draw_rect(struct displaylink_dev *dev, int x, int y, int width, int height,
          unsigned char red, unsigned char green, unsigned char blue);
void displaylink_destroy_framebuffer(struct displaylink_dev *dev);

char *displaylink_set_register(char *bufptr, uint8_t reg, uint8_t val);

int
displaylink_copyarea(struct displaylink_dev *dev, int dx, int dy, int sx, int sy,
         int width, int height);

int
displaylink_image_blit(struct displaylink_dev *dev, int x, int y, int width, int height,
           char *data);

int displaylink_blank(struct displaylink_dev *dev, int blankmode);

int displaylink_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg);

void displaylink_edid(struct displaylink_dev *dev);
