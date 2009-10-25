/* blit/compression functions */
/* all the real optimizations goes here */

/* thanks to Henrik Bjerregaard Pedersen for this function */

#ifndef DISPLAYLINK
        #include "displaylink.h"
#endif

static char *rle_compress16(uint16_t *src, char *dst, int rem) {

    int rl;
    uint16_t pix0;
    char *end_if_raw = dst + 6 + 2 * rem;

    dst += 6; // header will be filled in if RLE is worth it

    while (rem && dst < end_if_raw) {
        char *start = (char *)src;

        pix0 = *src++;
        rl = 1;
        rem--;
        while (rem && *src == pix0)
            rem--, rl++, src++;
        *dst++ = rl;
        *dst++ = start[1];
        *dst++ = start[0];
    }

    return dst;
}

/*
Thanks to Henrik Bjerregaard Pedersen for rle implementation and code refactoring.
Next step is huffman compression.
*/

int
displaylink_image_blit(struct displaylink_dev *dev, int x, int y, int width, int height,
	   char *data)
{

	int i, j, base;
	int rem = width;
	int ret;

	int firstdiff, thistime;

	char *bufptr;

	if (dev->udev == NULL) {
		return 0;
	}

	if (x + width > dev->fb_info->var.xres) {
		return -EINVAL;
	}

	if (y + height > dev->fb_info->var.yres *2) {	// hideo
		return -EINVAL;
	}

	mutex_lock(&dev->bulk_mutex);

	base = dev->base16 + ( (dev->line_length * y) + (x * 2) );

	data += (dev->line_length * y) + (x * 2);

	//printk("IMAGE_BLIT\n");

	bufptr = dev->buf;

	for (i = y; i < y + height; i++) {

		if (dev->bufend - bufptr < BUF_HIGH_WATER_MARK) {
			ret = displaylink_bulk_msg(dev, bufptr - dev->buf);
			bufptr = dev->buf;
		}

		rem = width;

		//printk("WRITING LINE %d\n", i);

		while (rem) {

			if (dev->bufend - bufptr < BUF_HIGH_WATER_MARK) {
				ret =
				    displaylink_bulk_msg(dev,
						  bufptr - dev->buf);
				bufptr = dev->buf;
			}

            // number of pixels to consider this time
            thistime = rem;
            if (thistime > 255)
                 thistime = 255;

            // find position of first pixel that has changed
			firstdiff = -1;
			for (j = 0; j < thistime * 2; j++) {
				if (dev->
				    backing_buffer[ base - dev->base16 + j] !=
				    data[j]) {
					firstdiff = j/2;
					break;
				}
			}

			if (firstdiff >= 0) {
                char *end_of_rle;

                end_of_rle =
                    rle_compress16((uint16_t *)(data + firstdiff * 2),
                                     bufptr, thistime-firstdiff);

                if (end_of_rle < bufptr + 6 + 2 * (thistime-firstdiff)) {
                    bufptr[0] = 0xAF;
		   		    bufptr[1] = 0x69;

				    bufptr[2] = (char)((base+firstdiff*2) >> 16);
				    bufptr[3] = (char)((base+firstdiff*2) >> 8);
				    bufptr[4] = (char)(base+firstdiff*2);
                    bufptr[5] = thistime-firstdiff;
 
                    bufptr = end_of_rle;

                } else {
                    // fallback to raw (or some other encoding?)
                    *bufptr++ = 0xAF;
		   		    *bufptr++ = 0x68;

				    *bufptr++ = (char)((base+firstdiff*2) >> 16);
				    *bufptr++ = (char)((base+firstdiff*2) >> 8);
				    *bufptr++ = (char)(base+firstdiff*2);
				    *bufptr++ = thistime-firstdiff;
				    // PUT COMPRESSION HERE
				    for (j = firstdiff * 2; j < thistime * 2; j += 2) {
					    *bufptr++ = data[j + 1];
					    *bufptr++ = data[j];
				    }
                }
			}

			base += thistime * 2;
			data += thistime * 2;
			rem -= thistime;
		}

		memcpy(dev->backing_buffer + (base-dev->base16) - (width * 2),
		       data - (width * 2), width * 2);

		base += (dev->line_length) - (width * 2);
		data += (dev->line_length) - (width * 2);

	}

	if (bufptr > dev->buf) {
		ret = displaylink_bulk_msg(dev, bufptr - dev->buf);
	}

	mutex_unlock(&dev->bulk_mutex);

	return base;

}
