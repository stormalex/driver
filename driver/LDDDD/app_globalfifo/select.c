#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>

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
		if(ioctl(fd, FIFO_CLEAR, 0) < 0)
			printf("ioctl command failed\n");
		
		while(1)
		{
			FD_ZERO(&rfds);
			FD_ZERO(&wfds);
			FD_SET(fd, &rfds);
			FD_SET(fd, &wfds);
			
			ret = select(fd + 1, &rfds, &wfds, NULL, NULL);
			
			if(FD_ISSET(fd, &rfds))
				printf("Poll monitor:can be read\n");
			
			if(FD_ISSET(fd, &wfds))
				printf("Poll monitor:can be write\n");
			
			sleep(3);
		}
	}
	else
		printf("Device open failure\n");
	
	return 0;
}