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

typedef struct {
    void *start;
    size_t length;
} Buffer;

int main(){
  int fd;
  struct v4l2_requestbuffers reqbuf;
  struct v4l2_buffer buffer;
  Buffer *buffers;
  unsigned int i;
  bool capture = true;

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
  printf("Device capabilities: %x",capabilities.device_caps);
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
    perror("Error: failed to read the image format.");
    exit(EXIT_FAILURE);
  }


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

      buffers[i].start = mmap(NULL, buffer.length,
          PROT_READ | PROT_WRITE, /* recommended */
          MAP_SHARED,             /* recommended */
          fd, buffer.m.offset);
      if (MAP_FAILED == buffers[i].start) {
          /* If you do not exit here you should unmap() and free()
             the buffers mapped so far. */
          printf("Hi");
          perror("mmap");
          printf("Hello");
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

  //Run the capture loop
  while(capture){
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


    printf("Buffer captured - Index: %d\n",index);

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
