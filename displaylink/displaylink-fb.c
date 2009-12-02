/* DisplayLink virtual framebuffer functions */

#ifndef DISPLAYLINK
        #include "displaylink.h"
#endif


/* memory functions taken from vfb */

static void *rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr;

	size = PAGE_ALIGN(size);
	mem = vmalloc_32(size);
	if (!mem)
		return NULL;

	memset(mem, 0, size);	/* Clear the ram out, no junk to the user */
	adr = (unsigned long)mem;
	while (size > 0) {
		SetPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	return mem;
}

static void rvfree(void *mem, unsigned long size)
{
	unsigned long adr;

	if (!mem)
		return;

	adr = (unsigned long)mem;
	while ((long)size > 0) {
		ClearPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	vfree(mem);
}

static int displaylink_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	unsigned long start = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long page, pos;

	printk("MMAP: %lu %u\n", offset + size, info->fix.smem_len);

	if (offset + size > info->fix.smem_len)
		return -EINVAL;

	pos = (unsigned long)info->fix.smem_start + offset;

	while (size > 0) {
		page = vmalloc_to_pfn((void *)pos);
		if (remap_pfn_range(vma, start, page, PAGE_SIZE, PAGE_SHARED))
			return -EAGAIN;

		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}

	vma->vm_flags |= VM_RESERVED;	/* avoid to swap out this VMA */
	return 0;

}


static void displaylink_fb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{

	struct displaylink_dev *dev = info->par ;


	mutex_lock(&dev->fb_mutex);

	dev = info->par;

	if (dev->udev == NULL)
		return;

	//printk("fb copy area\n");

	displaylink_copyarea(dev, area->dx, area->dy, area->sx, area->sy, area->width,
		 area->height);

	mutex_unlock(&dev->fb_mutex);
	//printk("fb copy area done\n");

}

static void displaylink_fb_imageblit(struct fb_info *info, const struct fb_image *image)
{

	struct displaylink_dev *dev = info->par;

	mutex_lock(&dev->fb_mutex);

	dev = info->par;

	if (dev->udev == NULL)
		return;

	//printk("fb image blit\n");

	cfb_imageblit(info, image);

	displaylink_image_blit(dev, image->dx, image->dy, image->width, image->height,
		       info->screen_base);
	mutex_unlock(&dev->fb_mutex);

	//printk("fb image done\n");
}

static void displaylink_fb_fillrect(struct fb_info *info, const struct fb_fillrect *region)
{

	unsigned char red, green, blue;
	struct displaylink_dev *dev = info->par ;

	mutex_lock(&dev->fb_mutex);

	dev = info->par;

	if (dev->udev == NULL)
		return;

	//printk("fb fill rect\n");

	memcpy(&red, &region->color, 1);
	memcpy(&green, &region->color + 1, 1);
	memcpy(&blue, &region->color + 2, 1);

	displaylink_draw_rect(dev, region->dx, region->dy, region->width, region->height,
		  red, green, blue);

	mutex_unlock(&dev->fb_mutex);

	//printk("fb fill rect done\n");

}

/* taken from vesafb */

static int
displaylink_setcolreg(unsigned regno, unsigned red, unsigned green,
	       unsigned blue, unsigned transp, struct fb_info *info)
{
	int err = 0;

	if (regno >= info->cmap.len)
		return 1;

	if (regno < 16) {
		if (info->var.red.offset == 10) {
			/* 1:5:5:5 */
			((u32 *) (info->pseudo_palette))[regno] =
			    ((red & 0xf800) >> 1) |
			    ((green & 0xf800) >> 6) | ((blue & 0xf800) >> 11);
		} else {
			/* 0:5:6:5 */
			((u32 *) (info->pseudo_palette))[regno] =
			    ((red & 0xf800)) |
			    ((green & 0xfc00) >> 5) | ((blue & 0xf800) >> 11);
		}
	}

	return err;
}


static int displaylink_fb_open(struct fb_info *info, int user) {

	struct displaylink_dev *dev = info->par;

	BUG_ON(dev == NULL);

	/* fbcon can survive disconnection, no refcount needed */
	if (user == 0) {
		return 0;
	}


	//printk("opening displaylink framebuffer...\n");
	mutex_lock(&dev->fb_mutex);

	//printk("application %s %d is trying to open displaylink device\n", current->comm, user);

	if (dev->udev == NULL) {
		mutex_unlock(&dev->fb_mutex);
		return -1;
	}

	atomic_inc(&dev->fb_count);

	//printk("fb count: %d\n", atomic_read(&dev->fb_count)); 

	mutex_unlock(&dev->fb_mutex);

	return 0;
	
}

static int displaylink_fb_release(struct fb_info *info, int user)
{
	struct displaylink_dev *dev = info->par;

	BUG_ON(dev == NULL);

	/* fbcon control */
	if (user == 0) {
		return 0;
	}
		
	//printk("releasing displaylink framebuffer...\n");
	mutex_lock(&dev->fb_mutex);

	atomic_dec(&dev->fb_count);

	//printk("release fb count: %d\n", atomic_read(&dev->fb_count)); 

	if (atomic_read(&dev->fb_count) == 0 && dev->udev == NULL) {
		displaylink_destroy_framebuffer(dev);
		mutex_unlock(&dev->fb_mutex);
		kfree(dev);
		return 0 ;	
	}


	displaylink_image_blit(dev, 0, 0, info->var.xres, info->var.yres,
		   info->screen_base);

	mutex_unlock(&dev->fb_mutex);

	return 0;
}

static int displaylink_fb_blank(int blank_mode, struct fb_info *info)
{
	struct displaylink_dev *dev = info->par;

	displaylink_blank(dev, blank_mode);

	return 0;
}

static int displaylink_fb_setpar(struct fb_info *info) {
	
	struct displaylink_dev *dev = info->par;
	
	BUG_ON(dev == NULL);

	if (dev->udev == NULL)
                return -EINVAL;

	//printk("setting hardware to %d %d\n", info->var.xres, info->var.yres);

	//displaylink_set_video_mode(dev, 0, info->var.xres, info->var.yres, 0);

	info->fix.line_length = dev->line_length;

	return 0;
}




static int displaylink_pan_display(struct fb_var_screeninfo *var,
		struct fb_info *fbi)
{
	int r = 0;
	struct displaylink_dev *dev = fbi->par;



	if (var->xoffset != fbi->var.xoffset ||
	    var->yoffset != fbi->var.yoffset) {
		struct fb_var_screeninfo new_var;

		new_var = fbi->var;
		new_var.xoffset = var->xoffset;
		new_var.yoffset = var->yoffset;

		//r = check_fb_var(fbi, &new_var);

		//if (r == 0) {
			fbi->var = new_var;
			//set_fb_fix(fbi);
			//r = omapfb_apply_changes(fbi, 0);
		//}
	}

	//if (display && display->update)
	//	display->update(display, 0, 0, var->xres, var->yres);
	//printk("displaylink_pan_display: var->xres %d var->yres %d fbi->var.xoffset %d fbi->var.yoffset %d\n", var->xres, var->yres, fbi->var.xoffset, fbi->var.yoffset);

	if (fbi->var.yoffset) 
	displaylink_image_blit(dev, 0, 0, var->xres, var->yres, fbi->screen_base + dev->screen_size/2);
	else
	displaylink_image_blit(dev, 0, 0, var->xres, var->yres, fbi->screen_base );
	return r;
}


static int displaylink_fb_checkvar(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct displaylink_dev *dev = info->par;
	struct edid *edid ;
	struct detailed_timing *best_edid ;
	struct std_timing *std_edid ;

	int i ;

	BUG_ON(dev == NULL);

	if (dev->udev == NULL)
		return -EINVAL;

	edid = (struct edid *) dev->edid ;

	//printk("checking for resolution %d %d\n", var->xres, var->yres);

	for(i=0;i<4;i++) {
		best_edid = (struct detailed_timing *) &edid->detailed_timings[i];
		if (EDID_GET_WIDTH(best_edid) == 0)
			break;
		//printk("edid %dX%d\n", EDID_GET_WIDTH(best_edid), EDID_GET_HEIGHT(best_edid));
		if (EDID_GET_WIDTH(best_edid) == var->xres && EDID_GET_HEIGHT(best_edid) == var->yres) {
			//printk("found valid resolution for displaylink device\n");
			return 0;
		}
	}

	for(i=0;i<8;i++) {
		std_edid = (struct std_timing *) &edid->standard_timings[i];
		if ((std_edid->hsize*8)+248 < 320) {
			break;
		}
		//printk("edid (std) %d %d %d %d\n", (std_edid->hsize*8)+248, (((std_edid->hsize*8)+248)/4)*3, std_edid->vfreq+60, std_edid->aspect_ratio);
		if ((std_edid->hsize*8)+248 == var->xres && (((std_edid->hsize*8)+248)/4)*3 == var->yres) {
			//printk("found valid resolution for displaylink device\n");
			return 0;
		}
	}
	
	
	return -EINVAL;
	
}

static struct fb_ops displaylink_fb_ops = {
	.fb_setcolreg = displaylink_setcolreg,
	.fb_fillrect = displaylink_fb_fillrect,
	.fb_copyarea = displaylink_fb_copyarea,
	.fb_imageblit = displaylink_fb_imageblit,
	.fb_mmap = displaylink_fb_mmap,
	.fb_ioctl = displaylink_ioctl,
	.fb_open  = displaylink_fb_open,
	.fb_release = displaylink_fb_release,
	.fb_blank = displaylink_fb_blank,
	.fb_check_var = displaylink_fb_checkvar,
	.fb_set_par = displaylink_fb_setpar,
	.fb_pan_display = displaylink_pan_display,	
};

int
displaylink_activate_framebuffer(struct displaylink_dev *dev, int mode)
{

	struct fb_info *info;

	info = framebuffer_alloc(sizeof(u32) * 256, &dev->udev->dev);

	if (!info) {
		printk("unable to allocate displaylink virtual framebuffer");
		return -ENOMEM;
	}

	dev->fb_info = info;

	info->pseudo_palette = info->par;
	info->par = dev;

	info->flags =
	    FBINFO_DEFAULT | FBINFO_READS_FAST | FBINFO_HWACCEL_IMAGEBLIT |
	    FBINFO_HWACCEL_COPYAREA | FBINFO_HWACCEL_FILLRECT;
	info->fbops = &displaylink_fb_ops;
	info->screen_base = rvmalloc(dev->screen_size);

	if (info->screen_base == NULL) {
		printk
		    ("cannot allocate framebuffer virtual memory of %d bytes\n",
		     dev->screen_size);
		goto out0;
	}

	fb_parse_edid(dev->edid, &info->var);

	info->var.bits_per_pixel = 16;
	info->var.activate = FB_ACTIVATE_TEST;
	info->var.vmode = FB_VMODE_NONINTERLACED|FB_VMODE_YWRAP;  // hideo
	info->var.yres_virtual = info->var.yres * 2;

	info->var.red.offset = 11;
	info->var.red.length = 5;
	info->var.red.msb_right = 0;

	info->var.green.offset = 5;
	info->var.green.length = 6;
	info->var.green.msb_right = 0;

	info->var.blue.offset = 0;
	info->var.blue.length = 5;
	info->var.blue.msb_right = 0;

	info->fix.smem_start = (unsigned long)info->screen_base;
	info->fix.smem_len = PAGE_ALIGN(dev->screen_size);
	
	if (strlen(dev->udev->product) > 15) {
		memcpy(info->fix.id, dev->udev->product, 15);
	}
	else {
		memcpy(info->fix.id, dev->udev->product, strlen(dev->udev->product));
	}
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.accel = info->flags;
	info->fix.line_length = dev->line_length;


	if (fb_alloc_cmap(&info->cmap, 256, 0) < 0)
		goto out1;

	printk("colormap allocated\n");
	if (register_framebuffer(info) < 0)
		goto out2;

	return 0;

 out2:
	fb_dealloc_cmap(&info->cmap);
 out1:
	rvfree(info->screen_base, dev->screen_size);
 out0:
	framebuffer_release(info);

	return -1 ;

}

void displaylink_destroy_framebuffer(struct displaylink_dev *dev) {

	printk("destroying framebuffer device...\n");
	unregister_framebuffer(dev->fb_info);
	printk("unregistering...\n");
	fb_dealloc_cmap(&dev->fb_info->cmap);
	printk("deallocating cmap...\n");
	rvfree(dev->fb_info->screen_base, dev->screen_size);
	printk("deallocating screen\n");
	framebuffer_release(dev->fb_info);
	printk("...done\n");
}
