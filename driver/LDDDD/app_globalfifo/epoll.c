#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <sys/epoll.h>

#define FIFO_CLEAR 0x01
#define BUFFER_LEN 20

int main(void)
{
	int fd, num;
	char rd_ch[BUFFER_LEN];
	fd_set rfds, wfds;
	int ret;
	struct timeval timeout;
	
	timeout.tv_sec = 2;
	timeout.tv_usec = 0;
	
	fd = open("/dev/globalfifo", O_RDONLY | O_NONBLOCK);		//非阻塞方式打开
	if(fd != -1)
	{
		struct epoll_event ev_globalfifo;
		int err;
		int epfd;
		
		if(ioctl(fd, FIFO_CLEAR, 0) < 0)
			printf("ioctl command failed\n");
		
		epfd = epoll_create(1);
		if(epfd < 0) {
			perror("epoll_create()");
			return -1;
		}
		
		bzero(&ev_globalfifo, sizeof(struct epoll_event));
		ev_globalfifo.events = EPOLLIN | EPOLLPRI;
		
		err = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev_globalfifo);
		if(err < 0) {
			perror("epoll_ctl()");
			return -1;
		}
		
		err = epoll_wait(epfd, &ev_globalfifo, 1, 15000);
		if(err < 0) {
			perror("epoll_wait()");
		} else if(err == 0) {
			printf("No data input in FIFO within 15 seconds.\n");
		} else {
			printf("FIFO is not empty\n");
		}
		
		err = epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ev_globalfifo);
		if(err < 0)
			perror("epoll_ctl()");
	}
	else
		printf("Device open failure\n");
	
	return 0;
}