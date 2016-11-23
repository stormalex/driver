TARGET=test_app
CC = arm-linux-gcc

CFLAGS := $(CFLAGS)

INCDIR += -I../../export
CFLAGS += $(INCDIR)

SRCS := $(wildcard *.c)
OBJS := $(patsubst %.c, %.o, $(SRCS))

#LIBS := -lipc
#LIBS += -Wl,-rpath=./

$(TARGET):$(OBJS)
	$(CC) $(OBJS) -ldl $(LIBS) -o $(TARGET)

%.o:%.c
	$(CC) -c $< $(CFLAGS) -o $@

%.d:%.c
	@set -e; rm -f $@; \
	$(CC) -MM $(CFLAGS) $< > $@; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@ > $@; \
	rm -f $@
-include $(SRCS:.c=.d)

.PHONY: clean install


clean:
	rm -f $(OBJS) $(TARGET) *.d
