/************************************************************************************
 *                          DisplayLink Kernel Driver                               *
 *                            Version 0.3 (was udlfb)                               *
 *             (C) 2009 Roberto De Ioris <roberto@unbit.it>                         *
 *                                                                                  *
 *     This file is licensed under the GPLv2. See COPYING in the package.           *
 * Based on the amazing work of Florian Echtler and libdlo 0.1                      *
 *                                                                                  *
 *                                                                           	    *	
 * 24.06.09 release 0.3 (resolution manager, new ioctls, renamed to displaylink.ko) *
 * 10.06.09 release 0.2.3 (edid ioctl, fallback for unsupported modes)              *
 * 05.06.09 release 0.2.2 (real screen blanking, rle compression, double buffer)    *
 * 31.05.09 release 0.2                                                      	    *
 * 22.05.09 First public (ugly) release                                             *
 ************************************************************************************/

#ifndef DISPLAYLINK
	#include "displaylink.h"
#endif

#define DRIVER_VERSION "DisplayLink 0.3"

static struct usb_device_id id_table[] = {
	{.idVendor = 0x17e9, .match_flags = USB_DEVICE_ID_MATCH_VENDOR,},
	{},
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver displaylink_driver;

static int
displaylink_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct displaylink_dev *dev;

	int ret;

	int mode = 0 ;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		printk("cannot allocate displaylink dev structure.\n");
		return -ENOMEM;
	}

	mutex_init(&dev->bulk_mutex);
	mutex_init(&dev->fb_mutex);

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;

	printk("DisplayLink device attached (%s)\n", dev->udev->product);

	/* add driver data to usb interface */
	usb_set_intfdata(interface, dev);

	dev->buf = kmalloc(BUF_SIZE, GFP_KERNEL);

	if (dev->buf == NULL) {
		printk("unable to allocate memory for displaylink commands\n");
		goto out;
	}
	dev->bufend = dev->buf + BUF_SIZE;

	dev->tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	usb_fill_bulk_urb(dev->tx_urb, dev->udev,
			  usb_sndbulkpipe(dev->udev, 1), dev->buf, 0,
			  displaylink_bulk_callback, dev);

	if (strlen(dev->udev->product) > 63) {
                memcpy(dev->name, dev->udev->product, 63);
        }
        else {
                memcpy(dev->name, dev->udev->product, strlen(dev->udev->product));
        }


	displaylink_edid(dev);
	ret = displaylink_setup(dev);


	displaylink_set_video_mode(dev, 0, 0, 0, 0);

	dev->backing_buffer = kzalloc(dev->screen_size, GFP_KERNEL);

	if (!dev->backing_buffer) {
		printk("non posso allocare il backing buffer\n");
		goto out;
	}

	ret = displaylink_activate_framebuffer(dev, mode);

	if (ret !=0) {
		printk("unable to allocate framebuffer\n");
		goto out;
	}




	// put the green screen
	displaylink_draw_rect(dev, 0, 0, dev->fb_info->var.xres,
		  dev->fb_info->var.yres, 0x30, 0xff, 0x30);

	return 0;

 out:
	usb_set_intfdata(interface, NULL);
	usb_put_dev(dev->udev);
	kfree(dev);
	return -ENOMEM;

}

static void displaylink_disconnect(struct usb_interface *interface)
{
	struct displaylink_dev *dev = usb_get_intfdata(interface);
	struct displaylink_orphaned_dev *odev;

	mutex_unlock(&dev->bulk_mutex);

	usb_kill_urb(dev->tx_urb);
	usb_free_urb(dev->tx_urb);
	usb_set_intfdata(interface, NULL);
	usb_put_dev(dev->udev);

	mutex_lock(&dev->fb_mutex);

	printk("fb count: %d\n", atomic_read(&dev->fb_count)); 

	if (atomic_read(&dev->fb_count) == 0) {
		displaylink_destroy_framebuffer(dev);
	}
	else {
		printk("the framebuffer associated to this displaylink device is still in use. postponing deallocation...\n");
		// mark the framebuffer for destruction
		odev = kzalloc(sizeof(*odev), GFP_KERNEL);
		atomic_set(&odev->fb_count, atomic_read(&dev->fb_count) );
		odev->udev = NULL;
		mutex_init(&odev->fb_mutex);
		odev->fb_info = dev->fb_info;
		odev->fb_info->par = odev;
		odev->screen_size = dev->screen_size;
		odev->line_length = dev->line_length;
		printk("%d clients are still connected to this framebuffer device\n", atomic_read(&odev->fb_count));
	}

	mutex_unlock(&dev->fb_mutex);

	kfree(dev);

	printk("DisplayLink device disconnected\n");
}

static struct usb_driver displaylink_driver = {
	.name = "displaylink",
	.probe = displaylink_probe,
	.disconnect = displaylink_disconnect,
	.id_table = id_table,
};

static int __init displaylink_init(void)
{
	int res;

	res = usb_register(&displaylink_driver);
	if (res)
		err("usb_register failed. Error number %d", res);

	return res;
}

static void __exit displaylink_exit(void)
{
	usb_deregister(&displaylink_driver);
}

module_init(displaylink_init);
module_exit(displaylink_exit);

MODULE_AUTHOR("Roberto De Ioris <roberto@unbit.it>");
MODULE_DESCRIPTION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
