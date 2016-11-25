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
	char buf[2048];
	int i = 0;
    
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
			do {
				ret = read(fd, buf, 4);
				printf("[%d]", ret);
				if(ret > 0) {
					for(i = 0; i < ret; i++) {
						printf(" [0x%02x]", buf[i]);
					}
				}
				printf("\n");
			}while(ret > 0);
		}
	}
	

#endif
    close(fd);
    return 0;
}