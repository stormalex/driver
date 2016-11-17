KERN_DIR=/home/nick/project/linux-3.4.2

all:
		make -C $(KERN_DIR) M=`pwd` modules

clean:
		make -C $(KERN_DIR) M=`pwd` modules clean
		rm -rf modules.order
		
obj-m += at24.o
obj-m += at24_dev.o