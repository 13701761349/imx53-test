
/*
 * Copyright 2004-2011 Freescale Semiconductor, Inc.
 *
 * Copyright (c) 2006, Chips & Media.  All rights reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/videodev.h>
#include "vpu_test.h"

#define TEST_BUFFER_NUM 3

#define OV5642_VGA	    0
#define OV5642_QVGA	    1
#define OV5642_NTSC	    2
#define OV5642_PAL	    3
#define OV5642_720P	    4
#define OV5642_1080P	    5

static int cap_fd = -1;
struct capture_testbuffer cap_buffers[TEST_BUFFER_NUM];

static int getCaptureMode(int width, int height)
{
	int mode = 0;

	if (width == 640 && height == 480)
		mode = OV5642_VGA;
	else if (width == 320 && height == 240)
		mode = OV5642_QVGA;
	else if (width == 720 && height == 480)
		mode = OV5642_NTSC;
	else if (width == 720 && height == 576)
		mode = OV5642_PAL;
	else if (width == 1280 && height == 720)
		mode = OV5642_720P;
	else if (width == 1920 && height == 1080)
		mode = OV5642_1080P;

	return mode;
}

int
v4l_start_capturing(void)
{
	unsigned int i;
	struct v4l2_buffer buf;
	enum v4l2_buf_type type;

	for (i = 0; i < TEST_BUFFER_NUM; i++) {
		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if (ioctl(cap_fd, VIDIOC_QUERYBUF, &buf) < 0) {
			err_msg("VIDIOC_QUERYBUF error\n");
			return -1;
		}

		cap_buffers[i].length = buf.length;
		cap_buffers[i].offset = (size_t) buf.m.offset;
	}

	for (i = 0; i < TEST_BUFFER_NUM; i++) {
		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		buf.m.offset = cap_buffers[i].offset;
		if (ioctl(cap_fd, VIDIOC_QBUF, &buf) < 0) {
			err_msg("VIDIOC_QBUF error\n");
			return -1;
		}
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(cap_fd, VIDIOC_STREAMON, &type) < 0) {
		err_msg("VIDIOC_STREAMON error\n");
		return -1;
	}

	return 0;
}

void
v4l_stop_capturing(void)
{
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ioctl(cap_fd, VIDIOC_STREAMOFF, &type);
	close(cap_fd);
	cap_fd = -1;
}

int
v4l_capture_setup(struct encode *enc, int width, int height, int fps)
{
	char v4l_device[32] = "/dev/video0";
	struct v4l2_format fmt = {0};
	struct v4l2_streamparm parm = {0};
	struct v4l2_requestbuffers req = {0};
	struct v4l2_control ctl;
	struct v4l2_crop crop;
	int g_input = 1;

	if (cap_fd > 0) {
		warn_msg("capture device already opened\n");
		return -1;
	}

	if ((cap_fd = open(v4l_device, O_RDWR, 0)) < 0) {
		err_msg("Unable to open %s\n", v4l_device);
		return -1;
	}

	ctl.id = V4L2_CID_PRIVATE_BASE;
	ctl.value = 0;
	if (ioctl(cap_fd, VIDIOC_S_CTRL, &ctl) < 0)
	{
		printf("set control failed\n");
		return -1;
	}

	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	parm.parm.capture.timeperframe.numerator = 1;
	parm.parm.capture.timeperframe.denominator = fps;
	parm.parm.capture.capturemode = getCaptureMode(width, height);
	if (ioctl(cap_fd, VIDIOC_S_PARM, &parm) < 0) {
		err_msg("set frame rate failed\n");
		close(cap_fd);
		cap_fd = -1;
		return -1;
	}

	if (ioctl(cap_fd, VIDIOC_S_INPUT, &g_input) < 0) {
		printf("VIDIOC_S_INPUT failed\n");
		close(cap_fd);
		return -1;
	}

	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	crop.c.width = width;
	crop.c.height = height;
	crop.c.top = 0;
	crop.c.left = 0;
	if (ioctl(cap_fd, VIDIOC_S_CROP, &crop) < 0) {
		printf("VIDIOC_S_CROP failed\n");
		close(cap_fd);
		return -1;
	}

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = width;
	fmt.fmt.pix.height = height;
	fmt.fmt.pix.bytesperline = width;
	fmt.fmt.pix.priv = 0;
	fmt.fmt.pix.sizeimage = 0;
	if (enc->cmdl->chromaInterleave == 0)
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
	else
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
	if (ioctl(cap_fd, VIDIOC_S_FMT, &fmt) < 0) {
		err_msg("set format failed\n");
		close(cap_fd);
		return -1;
	}

	memset(&req, 0, sizeof(req));
	req.count = TEST_BUFFER_NUM;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (ioctl(cap_fd, VIDIOC_REQBUFS, &req) < 0) {
		err_msg("v4l_capture_setup: VIDIOC_REQBUFS failed\n");
		close(cap_fd);
		cap_fd = -1;
		return -1;
	}

	return 0;
}

int
v4l_get_capture_data(struct v4l2_buffer *buf)
{
	memset(buf, 0, sizeof(struct v4l2_buffer));
	buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf->memory = V4L2_MEMORY_MMAP;
	if (ioctl(cap_fd, VIDIOC_DQBUF, buf) < 0) {
		err_msg("VIDIOC_DQBUF failed\n");
		return -1;
	}

	return 0;
}

void
v4l_put_capture_data(struct v4l2_buffer *buf)
{
	ioctl(cap_fd, VIDIOC_QBUF, buf);
}

