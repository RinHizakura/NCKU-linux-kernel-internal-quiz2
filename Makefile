all: xs.o

xs.o: xs.c
	gcc -o xs -std=gnu11 xs.c
