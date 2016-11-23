#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>   
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>


#define USE_SELECT

int main(int argc, char* argv[])
{
    int fd = 0;
	char buf[10];
    
    if(argv[1] == NULL) {
        printf("Usage:app /dev/usb_mouse_xx\n");
    }
    
    fd = open(argv[1], O_RDONLY);
    if(fd < 0) {
        printf("open %s failed\n", argv[1]);
        return -1;
    }
    

#ifdef USE_SELECT
	int ret = 0;
	fd_set rd_set;
	int maxfd = fd + 1;

	while(1) {
		FD_ZERO(&rd_set);
		FD_SET(fd, &rd_set);
		ret = select(maxfd, &rd_set, NULL, NULL, NULL);
		if(ret == 0) {
			printf("select timeout\n");
			sleep(1);
			continue;
		}
		else if(ret < 0) {
			printf("select failured, ret=%d\n", ret);  
            continue;  
		}
		printf("select OK, ret=%d\n", ret);
		if(FD_ISSET(fd, &rd_set)) {
			printf("fd=%d can read\n", fd);
			ret = read(fd, buf, 4);
			printf("read %d bytes data: %c %c\n", ret, buf[0], buf[1]);
		}
	}
	

#endif
    close(fd);
    return 0;
}