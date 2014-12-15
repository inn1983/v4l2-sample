#include <linux/videodev2.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ppm.h>

#include "yuyv_to_rgb.h"

#define CAMERA_DEV   "/dev/video0"
#define CAMERA_WIDTH  640
#define CAMERA_HEIGHT 480
#define MMAP_COUNT    2
#define PICTURE_NUM   1
#define PPM_FILENAME  "camera%02d.ppm"
#define YUV422_FILENAME  "ezcap.yuv"
#define NV12_FILENAME  "ezcap_nv12.yuv"


struct buffer {
        void *                  start;
        size_t                  length;
};
struct buffer *         mmap_buffers         = NULL;

static void ppm_writefile(uint8_t *prgb, int width, int height, int num)
{
  pixel *pixelrow;
  int i, x, y;

  pixelrow = ppm_allocrow(width);

  for (i = 0; i < num; i++) {
    char filename[16];
    FILE *fp;

    sprintf(filename, PPM_FILENAME, i);
    fp = fopen(filename, "w");

    ppm_writeppminit(fp, width, height, (pixval)255, 0);
    for (y = 0; y < height; y++) {
      for (x = 0; x < width; x++, prgb += 3)
	PPM_ASSIGN(pixelrow[x], prgb[0], prgb[1], prgb[2]);
      ppm_writeppmrow(fp, pixelrow, width, (pixval)255, 0);
    }

    fclose(fp);
  }

  ppm_freerow(pixelrow);
}


static void yuv420_writefile(uint8_t *prgb, int width, int height, int num)
{

}
#if 0
void uyvu_to_yuv420( 
 int y_stride, // Y stride of I420, in pixel 
 int uv_stride, // U and V stride of I420, in pixel 
 uint8_t *input_img, // input UYVY image 
 uint8_t *output_img, //output 420 image
 int width, // image width 
 int height // image height 
 ) 
{ 
	int row; 
	int col; 
	uint8_t *pImg = output_img;
	uint8_t *y_plane;
	uint8_t *u_plane;
	uint8_t *v_plane;
	
	y_plane = output_img;
	u_plane = output_img + height * width;
	v_plane = u_plane + (height * width)/4;
	
	for (row = 0; row < height; row=row+1)
		for (col = 0; col < width; col=col+2){
			u_plane[row/2 * uv_stride + col/2] = input_img[0];
			y_plane[row * y_stride + col] = input_img[1];
			v_plane[row/2 * uv_stride + col/2] = input_img[2];
			y_plane[row * y_stride + col + 1] = input_img[3];
			
			pImg += 4;
		}
	
}
#endif

static void yuyv_nv12(const unsigned char *pyuyv, unsigned char *pnv12, int width, int height)
{
	unsigned char *Y = pnv12;
	unsigned char *UV = Y + width * height;
	//unsigned char *V = U + width * height / 4;
	int i, j;

	for (i = 0; i < height / 2; i++) {
		// 奇数行保留 U/V
		for (j = 0; j < width / 2; j++) {
			*UV++ = *pyuyv++;	//U
			*Y++ = *pyuyv++;
			*UV++ = *pyuyv++;	//V
			*Y++ = *pyuyv++;
		}

		// 偶数行的 UV 直接扔掉
		for (j = 0; j < width / 2; j++) {
			pyuyv++;		// 跳过 U
			*Y++ = *pyuyv++;
			pyuyv++;		// 跳过 V
			*Y++ = *pyuyv++;
		}
	}

#ifdef DUMP_YUV420P
	// 使用 ffmpeg -s <width>x<height> -pix_fmt yuv420p -i img-xx.yuv img-xx.jpg
	// 然后检查 img-xx.jpg 是否正确？
	// 经过检查，此处 yuv420p 图像正常，这么说，肯定是 GetFrmBufCB() 里面设置问题了 :(
#define CNT 10
	static int _ind = 0;
	char fname[128];
	snprintf(fname, sizeof(fname), "img-%02d.yuv", _ind);
	_ind++;
	_ind %= CNT;
	FILE *fp = fopen(fname, "wb");
	if (fp) {
		fwrite(q, 1, width*height*3/2, fp);
		fclose(fp);
	}
#endif // 
}

static int xioctl(int fd, int request, void *arg)
{
  for (; ; ) {
    int ret = ioctl(fd, request, arg);
    if (ret < 0) {
      if (errno == EINTR)
	continue;
      return -errno;
    }
    break;
  }

  return 0;
}

int main(void)
{
  int fd, width, height, length, count, ret, i;
  struct v4l2_format fmt;
  struct v4l2_requestbuffers req;
  struct v4l2_buffer buf;
  enum v4l2_buf_type type;
  //void *mmap_p[MMAP_COUNT];
  //__u32 mmap_l[MMAP_COUNT];
  uint8_t *yuyvbuf, *rgbbuf, *pyuyv, *prgb;
  uint8_t *yuv420buf;
  struct rusage usage;
  double t;
	
  uint8_t testbuf[100];
  testbuf[0] = 0xff;
  
  fd = open(CAMERA_DEV, O_RDWR, 0);
  if (fd < 0) {
    perror("open");
    return -1;
  }

  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = CAMERA_WIDTH;
  fmt.fmt.pix.height = CAMERA_HEIGHT;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
  //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
  //fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
  ret = xioctl(fd, VIDIOC_S_FMT, &fmt);
  if (ret < 0 || fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV ||
      fmt.fmt.pix.width <= 0 || fmt.fmt.pix.height <= 0) {
    perror("ioctl(VIDIOC_S_FMT)");
    return -1;
  }
  width = fmt.fmt.pix.width;
  height = fmt.fmt.pix.height;
  length = width * height;

  yuyvbuf = malloc(2 * length * PICTURE_NUM);
  if (!yuyvbuf) {
    perror("malloc");
    return -1;
  }
  
  //yuv420buf = malloc(length + length/2 * PICTURE_NUM);

  memset(&req, 0, sizeof(req));
  req.count = MMAP_COUNT;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;
  ret = xioctl(fd, VIDIOC_REQBUFS, &req);
  if (ret < 0) {
    perror("ioctl(VIDIOC_REQBUFS)");
    return -1;
  }
  count = req.count;
  mmap_buffers = calloc (req.count, sizeof (*mmap_buffers));

  for (i = 0; i < count; i++) {
    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index  = i;
    ret = xioctl(fd, VIDIOC_QUERYBUF, &buf);
    if (ret < 0) {
      perror("ioctl(VIDIOC_QUERYBUF)");
      return -1;
    }

    //mmap_p[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
	mmap_buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
    if (mmap_buffers[i].start == MAP_FAILED) {
      perror("mmap");
      return -1;
    }
    mmap_buffers[i].length = buf.length;
  }

  for (i = 0; i < count; i++) {
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;
    ret = xioctl(fd, VIDIOC_QBUF, &buf);
    if (ret < 0) {
      perror("ioctl(VIDIOC_QBUF)");
      return -1;
    }
  }

  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  ret = xioctl(fd, VIDIOC_STREAMON, &type);
  if (ret < 0) {
    perror("ioctl(VIDIOC_STREAMON)");
    return -1;
  }

  for (i = 0, pyuyv = yuyvbuf; i < PICTURE_NUM; i++) {
    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    for (; ; ) {
      ret = select(fd + 1, &fds, NULL, NULL, NULL);
      if (ret < 0) {
	if (errno == EINTR)
	  continue;
	perror("select");
	return -1;
      }
      break;
    }

    if (FD_ISSET(fd, &fds)) {
      memset(&buf, 0, sizeof(buf));
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      ret = xioctl(fd, VIDIOC_DQBUF, &buf);
      if (ret < 0 || buf.bytesused < (__u32)(2 * length)) {
	perror("ioctl(VIDOC_DQBUF)");
	return -1;
      }

      memcpy(pyuyv, mmap_buffers[i].start, 2 * length);
	  
      pyuyv += 2 * length;

      ret = xioctl(fd, VIDIOC_QBUF, &buf);
      if (ret < 0) {
	perror("ioctl(VIDIOC_QBUF)");
	return -1;
      }
    }
  }

  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  xioctl(fd, VIDIOC_STREAMOFF, &type);
  for (i = 0; i < count; i++)
    munmap(mmap_buffers[i].start, mmap_buffers[i].length);
  close(fd);

  rgbbuf = malloc(3 * length * PICTURE_NUM);
  if (!rgbbuf) {
    perror("malloc");
    return -1;
  }

  getrusage(RUSAGE_SELF, &usage);
  t = ((double)usage.ru_utime.tv_sec * 1e+3 +
       (double)usage.ru_utime.tv_usec * 1e-3);

  for (i = 0, pyuyv = yuyvbuf; i < PICTURE_NUM;
       i++, pyuyv += 2 * length)
  {
	FILE * file_fd;
	unsigned char *pnv12 = malloc(length + length/2 * PICTURE_NUM);
	yuyv_nv12(pyuyv, pnv12, width, height);
	
	file_fd = fopen(NV12_FILENAME, "w");//?片文件名
	//yuyv_to_rgb(pyuyv, prgb, length);
	fwrite(pnv12, length*3/2, 1, file_fd); //将其写入文件中
	fclose(file_fd);
	free(pnv12);
#if 1
	file_fd = fopen(YUV422_FILENAME, "w");//?片文件名
	fwrite(pyuyv, length*2, 1, file_fd); //将其写入文件中
	fclose(file_fd);
#endif
  }
  
	   
 
 
/*
  getrusage(RUSAGE_SELF, &usage);
  t = ((double)usage.ru_utime.tv_sec * 1e+3 +
       (double)usage.ru_utime.tv_usec * 1e-3) - t;
  printf("convert time: %3.3lf msec/flame\n", t / (double)PICTURE_NUM);

  free(yuyvbuf);

  ppm_writefile(rgbbuf, width, height, PICTURE_NUM);

  free(rgbbuf);
*/
  free(yuyvbuf);

  return 0;
}
