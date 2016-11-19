#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc, char* argv[])
{
    int fd = 0;
    
    if(argv[1] == NULL) {
        printf("Usage:app /dev/usb_mouse_xx\n");
    }
    
    fd = open(argv[1], O_RDONLY);
    if(fd < 0) {
        printf("open %s failed\n", argv[1]);
        return -1;
    }
    
    while(1)
        sleep(10);
    
    close(fd);
    return 0;
}