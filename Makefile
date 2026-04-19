ifeq ($(OS),Windows_NT)
    CC=x86_64-w64-mingw32-gcc
else
    CC=gcc
endif
VERSION?=1.0.0
CFLAGS=-g -O3 -Wall -DSSDV_VERSION=\"$(VERSION)\"
LDFLAGS=-g

all: ssdv

ssdv: main.o ssdv.o rs8.o ssdv.h rs8.h
	$(CC) $(LDFLAGS) main.o ssdv.o rs8.o -o ssdv

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

install: all
	mkdir -p ${DESTDIR}/usr/bin
	install -m 755 ssdv ${DESTDIR}/usr/bin

clean:
	rm -f *.o ssdv