#include <iostream>               //nothrow
#include <csignal>                //sigint handling
#include <fcntl.h>                //open
#include <unistd.h>               //close
#include <stdio.h>                //printf
#include <stdlib.h>               //calloc, exit, free
#include <string.h>               //memset, strcmp
#include <assert.h>               //assert
#include <errno.h>                //errno
#include <sys/ioctl.h>            //ioctl
#include <sys/mman.h>             //mmap, munmap
#include <linux/v4l2-common.h>    //video4linux api
#include <linux/v4l2-controls.h>  //video4linux api
#include <linux/videodev2.h>      //video4linux api
#include <GL/freeglut.h>          //opengl rendering
#include <GL/gl.h>                //opengl rendering

//To Compile:
//   g++ capture.cpp -lglut -lGL -lGLEW -lGLU -o demo

//Structs
struct Buffer{
    void *start;
    size_t length;
};

//Global variables:
int fd;
struct v4l2_requestbuffers reqbuf;
struct v4l2_buffer buffer;
Buffer *buffers;
unsigned int num_mapped;
__u32 width;
__u32 height;
__u32 num_pixels;
__u8 *out_buff;
__u32 out_buff_len;

inline __u8 clip(__u32 a){
  return a > 255 ? 255 : a;
}

void convertYUYVtoRGB(__u8 *ptrIn, __u8 *ptrOut, __u32 num_pixels){
  for (int i = 0;  i < num_pixels/2;  ++i)
  {
    int y0 = ptrIn[0];
    int u0 = ptrIn[1];
    int y1 = ptrIn[2];
    int v0 = ptrIn[3];
    ptrIn += 4;
    int c = y0 - 16;
    int d = u0 - 128;
    int e = v0 - 128;
    ptrOut[0] = clip(( 298 * c + 516 * d + 128) >> 8);// blue
    ptrOut[1] = clip(( 298 * c - 100 * d - 208 * e + 128) >> 8); // green
    ptrOut[2] = clip(( 298 * c + 409 * e + 128) >> 8); // red
    c = y1 - 16;
    ptrOut[3] = clip(( 298 * c + 516 * d + 128) >> 8); // blue
    ptrOut[4] = clip(( 298 * c - 100 * d - 208 * e + 128) >> 8); // green
    ptrOut[5] = clip(( 298 * c + 409 * e + 128) >> 8); // red
    ptrOut += 6;
  }
}

void render(){
  //Run the capture loop
  while(true){
    //Initate a deque (and wait for a buffer to be available for deque)
    __u32 index;
    memset(&buffer, 0, sizeof(buffer));
    buffer.type = reqbuf.type;
    buffer.memory = V4L2_MEMORY_MMAP;
    if (-1 == ioctl(fd, VIDIOC_DQBUF, &buffer)) {
      perror("Error: ioct VIDIOC_DQBUF\n");
      exit(EXIT_FAILURE);
    }
    index = buffer.index; //keep track of which buffer deques

    //Convert the YUYV frame to RBB
    convertYUYVtoRGB((__u8*)buffers[index].start, out_buff, num_pixels);

    //Render with opengl
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);
    //TODO: Invert pixel drawing so the image isn't upside down!
    glDrawPixels(width , height, GL_BGR, GL_UNSIGNED_BYTE, out_buff );
    glutSwapBuffers();

    //After using the buffer, enque it to make it avialble to the driver again
    memset(&buffer, 0, sizeof(buffer));
    buffer.type = reqbuf.type;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = index;
    if (-1 == ioctl(fd, VIDIOC_QBUF, &buffer)) {
      perror("Error: ioctl VIDIOC_QBUF\n");
      exit(EXIT_FAILURE);
    }
  }
}

void int_handler(int x)
{
  unsigned int i;

  //Stop capture
  if (-1 == ioctl(fd, VIDIOC_STREAMOFF, &(reqbuf.type))){
    perror("Error: ioctl VIDIOC_STREAMOFF\n");
  }

  //Cleanup buffers
  for (i = 0; i < num_mapped; i++){
    munmap(buffers[i].start, buffers[i].length);
  }
  free(buffers);

  //Close the file
  close(fd);
}

int main(int argc, char* argv[]){
  unsigned int i;

  signal(SIGINT,int_handler);

  //Open the web camera device node
  fd = open("/dev/video0",O_RDWR);
  if (fd < 0){
    perror("Error: failed to open /dev/video0.\n");
    goto exit_noclose;
  }

  //Query the capabilities of the camera device
  struct v4l2_capability capabilities;
  if (-1 == ioctl(fd,VIDIOC_QUERYCAP,&capabilities)){
    perror("Error: failed to read V4L2 capabilities.\n");
    goto exit_close;
  }
  printf("Device capabilities: %x\n",capabilities.device_caps);
  //Device capabilities of toshiba laptop webcam = 0x04200001:
  // 0x00000001 - V4L2_CAP_VIDEO_CAPTURE:  Device supports the single planar AP through the video capture interface.
  // 0x00200000 - V4L2_CAP_EXT_PIX_FORMAT:  The device supports the struct v4l2_pix_format extended fields
  // 0x04000000 - V4L2_CAP_STREAMING: Device supports the streaming I/O method.
  if (0 == (capabilities.device_caps & 0x00000001)){
    perror("Error: camera device does not support V4L2_CAP_VIDEO_CAPTURE\n");
    goto exit_close;
  }
  if (0 == (capabilities.device_caps & 0x04000000)){
    perror("Error: camera device does not support V4L2_CAP_STREAMING\n");
    goto exit_close;
  }

  //Query the image format
  struct v4l2_format format;
  memset(&format, 0, sizeof(format));
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == ioctl (fd, VIDIOC_G_FMT, &format)){
    perror("Error: failed to read default image format.\n");
    goto exit_close;
  }
  width = format.fmt.pix.width;
  height = format.fmt.pix.height;
  num_pixels = width * height;
  printf("Camera Width (Pixels): %d\n", width);
  printf("Camera Height (Pixels): %d\n", height);
  union {
    char str[5];
    __u32 code;
  }pixFormat;
  pixFormat.code = format.fmt.pix.pixelformat;
  pixFormat.str[4] = 0;
  printf("Camera Format: %s\n",pixFormat.str);
  if (0 != strcmp(pixFormat.str,"YUYV")){
    perror("Error: Camera is not YUYU format. This demo assumes YUYV.\n");
    goto exit_close;
  }

  //Request the allocation of device buffers
  memset(&reqbuf, 0, sizeof(reqbuf));
  reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  reqbuf.memory = V4L2_MEMORY_MMAP;
  reqbuf.count = 20;
  if (-1 == ioctl(fd, VIDIOC_REQBUFS, &reqbuf)) {
    if (errno == EINVAL) {
      printf("Error: Video capturing or mmap-streaming is not supported\n");
    }
    else {
      perror("Error: ioctl VIDIOC_REQBUFS\n");
    }
    goto exit_close;
  }

  //Require that at least 5 buffers are created
  if (reqbuf.count < 5) {
    perror("Error: Not enough buffer memory\n");
    goto exit_close;
  }

  //Allocate buffer pointers for memory mapped storage
  buffers = (Buffer*) calloc(reqbuf.count, sizeof(*buffers));
  if (NULL == buffers){
    perror("Error: failed to allocate memory.\n");
    goto exit_close;
  }

  //Memory map the device bufffers into the program address space
  for (i = 0; i < reqbuf.count; i++) {
    memset(&buffer, 0, sizeof(buffer));
    buffer.type = reqbuf.type;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = i;
    if (-1 == ioctl(fd, VIDIOC_QUERYBUF, &buffer)) {
      perror("Error: ioctl VIDIOC_QUERYBUF\n");
      goto exit_unmap_free;
    }

    buffers[i].start = mmap(NULL, buffer.length,
    PROT_READ | PROT_WRITE,
    MAP_SHARED,
    fd, buffer.m.offset);
    if (MAP_FAILED == buffers[i].start) {
      perror("Error: failed to memory map buffer\n");
      goto exit_unmap_free;
      //mmap failed at this buffer index, so no need to unmap it
    }
    //mmap succeeded at this buffer index
    num_mapped++; //Track number of successfully mapped buffers
  }

  //Enque all buffers so they are accessible to the driver at startup
  for (i=0; i<reqbuf.count; i++){
    memset(&buffer, 0, sizeof(buffer));
    buffer.type = reqbuf.type;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = i;
    if (-1 == ioctl(fd, VIDIOC_QBUF, &buffer)) {
      perror("Error: ioctl VIDIOC_QBUF\n");
      goto exit_unmap_free;
    }
  }

  //Start capture
  if (-1 == ioctl(fd, VIDIOC_STREAMON, &(reqbuf.type))){
    perror("Error: ioctl VIDIOC_STREAMON\n");
    goto exit_unmap_free;
  }

  //Allocate buffer used for output conversion to RGB
  out_buff_len = width * height * 3;
  out_buff = new (std::nothrow) __u8[out_buff_len];
  if (NULL == out_buff){
    perror("Error: failed to allocate output buffer.\n");
    goto exit_stop_capture;
  }

  //Inialize opengl and start the main loop
  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_DOUBLE);
  glutInitWindowSize(640,480);
  glutInitWindowPosition(100,100);
  glutCreateWindow("Webcam Capture");
  glutDisplayFunc(render);
  glutMainLoop();

//glutMainLoop() runs until the program is terminated.
//The following error cases are therefore only reached on error.
//They are designed to "unwrap" all previous initialization
exit_stop_capture:
  if (-1 == ioctl(fd, VIDIOC_STREAMOFF, &(reqbuf.type))){
    perror("Error - ioctl VIDIOC_STREAMOFF\n");
  }
exit_unmap_free:
  //Unmaps all buffers that were successfully mapped
  //and frees the buffer pointers
  for (i = 0; i < num_mapped; i++){
    munmap(buffers[i].start, buffers[i].length);
  }
  free(buffers);
exit_close:
  close(fd);
exit_noclose:
  exit(EXIT_FAILURE);
}
