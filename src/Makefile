CFLAGS= -g -O2 -Wall `pkg-config --cflags libevdev lua53`
LDLIBS= `pkg-config --libs libevdev lua53`

lukeymap:	lukeymap.o monitor.o poll_group.o lua_device.o


clean:
	rm -f lukeymap *.o

.PHONY:	clean
