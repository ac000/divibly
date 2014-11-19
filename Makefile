C=gcc
CFLAGS=-Wall -g -std=c99 -O2
INCS=`pkg-config --cflags gtk+-2.0 libvlc`
LIBS=`pkg-config --libs gtk+-2.0 libvlc` -lxosd

divibly: divibly.c
	$(CC) $(CFLAGS) -o divibly divibly.c $(INCS) $(LIBS)

clean:
	rm -f divibly
