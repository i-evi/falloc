CC = gcc

CFLAGS += # -g
CFLAGS += # -fsanitize=address
CFLAGS += -O2 -flto

ALL = test scan

all: $(ALL)

%: %.c falloc.o bmpalloc.o
	$(CC) -o $@ $^ $(CFLAGS)
falloc.o: falloc.c falloc.h
	$(CC) -c -o $@ $< $(CFLAGS)
bmpalloc.o: bmpalloc.c bmpalloc.h
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(ALL) *.o
