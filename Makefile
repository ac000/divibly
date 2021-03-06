CC=gcc
CFLAGS=-Wall -Wextra -g -std=c99 -O2 -fstack-protector-strong -fPIC -Wl,-z,now -pie
INCS=`pkg-config --cflags gtk+-3.0 libvlc`
LIBS=`pkg-config --libs gtk+-3.0 libvlc` -lxosd -lrt

divibly: divibly.c
	$(CC) $(CFLAGS) -o divibly divibly.c $(INCS) $(LIBS)

clean:
	rm -f divibly
