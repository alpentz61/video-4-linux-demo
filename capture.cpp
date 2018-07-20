#include <iostream>
#include <fcntl.h>
#include <unistd.h>               //close
#include <stdio.h>                //printf
#include <stdlib.h>               //calloc, exit, free
#include <string.h>               //memset
#include <assert.h>               //assert
#include <errno.h>                //errno
#include <sys/ioctl.h>            //ioctl
#include <sys/mman.h>
#include <linux/v4l2-common.h>
#include <linux/v4l2-controls.h>
#include <linux/videodev2.h>
#include <GL/freeglut.h>          //opengl rendering
#include <GL/gl.h>                //opengl rendering

//To Compile:
//   g++ capture.cpp -lglut -lGL -lGLEW -lGLU -o demo

typedef struct {
    void *start;
    size_t length;
} Buffer;

int fd;
struct v4l2_requestbuffers reqbuf;
struct v4l2_buffer buffer;
Buffer *buffers;
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
    if (-1 == ioctl (fd, VIDIOC_DQBUF, &buffer)) {
        perror("VIDIOC_DQBUF");
        exit(EXIT_FAILURE);
    }
    index = buffer.index; //keep track of which buffer deques

    //Convert the YUYV frame to RBB
    convertYUYVtoRGB((__u8*)buffers[index].start, out_buff, num_pixels);

    //Render with opengl
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawPixels(width , height, GL_BGR, GL_UNSIGNED_BYTE, out_buff );
    glutSwapBuffers();

    //After using the buffer, enque it to make it avialble to the driver again
    memset(&buffer, 0, sizeof(buffer));
    buffer.type = reqbuf.type;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = index;
    if (-1 == ioctl (fd, VIDIOC_QBUF, &buffer)) {
        perror("VIDIOC_QBUF");
        exit(EXIT_FAILURE);
    }
  }
}
int main(int argc, char* argv[]){
  unsigned int i;

  //Open the camera device node
  fd = open("/dev/video0",O_RDWR);
  if (fd < 0){
    perror("Error: failed to open /dev/video0");
    exit(EXIT_FAILURE);
  }

  //Query the capabilities of the camera device
  struct v4l2_capability capabilities;
  if (-1 == ioctl(fd,VIDIOC_QUERYCAP,&capabilities)){
    perror("Error: reading V4L2 capabilities failed.");
    exit(EXIT_FAILURE);
  }
  printf("Device capabilities: %x\n",capabilities.device_caps);
  //Device capabilities = 0x04200001:
  // 0x00000001 - V4L2_CAP_VIDEO_CAPTURE:  Device supports the single planar AP through the video capture interface.
  // 0x00200000 - V4L2_CAP_EXT_PIX_FORMAT:  The device supports the struct v4l2_pix_format extended fields
  // 0x04000000 - V4L2_CAP_STREAMING: Device supports the streaming I/O method.
  //TODO: Perform checks that the device supports the above bit fields

  //Query the image format
  struct v4l2_format format;
  memset(&format, 0, sizeof(format));
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == ioctl (fd, VIDIOC_G_FMT, &format)){
    perror("Error: failed to read default image format.");
    exit(EXIT_FAILURE);
  }
  width = format.fmt.pix.width;
  height = format.fmt.pix.height;
  num_pixels = width * height;
  printf("Code %d\n",format.fmt.pix.pixelformat);
  printf("Camera Width (Pixels): %d\n", width);
  printf("Camera Height (Pixels): %d\n", height);
  union {
    char str[5];
    __u32 code;
  }pixFormat;
  pixFormat.code = format.fmt.pix.pixelformat;
  pixFormat.str[4] = 0;
  printf("Camera Format: %s\n",pixFormat.str);

  memset(&reqbuf, 0, sizeof(reqbuf));
  reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  reqbuf.memory = V4L2_MEMORY_MMAP;
  reqbuf.count = 20;
  if (-1 == ioctl (fd, VIDIOC_REQBUFS, &reqbuf)) {
      if (errno == EINVAL)
          printf("Video capturing or mmap-streaming is not supported\\n");
      else
          perror("VIDIOC_REQBUFS");

      exit(EXIT_FAILURE);
  }

  //Require at least 5 buffers
  if (reqbuf.count < 5) {
      /* You may need to free the buffers here. */
      printf("Not enough buffer memory\\n");
      exit(EXIT_FAILURE);
  }

  buffers = (Buffer*) calloc(reqbuf.count, sizeof(*buffers));
  assert(buffers != NULL);

  //Memory map the device bufffers into the program address space
  for (i = 0; i < reqbuf.count; i++) {
      memset(&buffer, 0, sizeof(buffer));
      buffer.type = reqbuf.type;
      buffer.memory = V4L2_MEMORY_MMAP;
      buffer.index = i;
      if (-1 == ioctl (fd, VIDIOC_QUERYBUF, &buffer)) {
          perror("VIDIOC_QUERYBUF");
          exit(EXIT_FAILURE);
      }

      buffers[i].length = buffer.length; /* remember for munmap() */
      printf("Buffer length: %d\n",buffer.length);

      buffers[i].start = mmap(NULL, buffer.length,
          PROT_READ | PROT_WRITE, /* recommended */
          MAP_SHARED,             /* recommended */
          fd, buffer.m.offset);
      if (MAP_FAILED == buffers[i].start) {
          /* If you do not exit here you should unmap() and free()
             the buffers mapped so far. */
          perror("mmap");
          exit(EXIT_FAILURE);
      }
  }

  //Enque all buffers so they are accessible to the driver at startup
  for (i=0; i<reqbuf.count; i++){
    memset(&buffer, 0, sizeof(buffer));
    buffer.type = reqbuf.type;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = i;
    if (-1 == ioctl (fd, VIDIOC_QBUF, &buffer)) {
        perror("VIDIOC_QBUF");
        exit(EXIT_FAILURE);
    }
  }

  //Start capture
  if (-1 == ioctl (fd, VIDIOC_STREAMON, &(reqbuf.type))){
    perror("VIDIOC_STREAMON");
    exit(EXIT_FAILURE);
  }

  //Allocate buffer used for output conversion to RGB
  out_buff_len = width * height * 3;
  out_buff = new (std::nothrow) __u8[out_buff_len];
  if (NULL == out_buff){
    perror("Unable to allocate output buffer.");
    exit(EXIT_FAILURE);
  }

  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_DOUBLE);
  glutInitWindowSize(640,480);
  glutInitWindowPosition(100,100);
  glutCreateWindow("OpenGL - First window demo");
  glutDisplayFunc(render);
  glutMainLoop();

  //TODO: move this cleanup to a sigint handler so that it gets callled
  //at program close.

  //Stop capture
  if (-1 == ioctl (fd, VIDIOC_STREAMOFF, &(reqbuf.type))){
    perror("VIDIOC_STREAMOFF");
    exit(EXIT_FAILURE);
  }

  //Cleanup
  for (i = 0; i < reqbuf.count; i++){
      munmap(buffers[i].start, buffers[i].length);
  }
}
