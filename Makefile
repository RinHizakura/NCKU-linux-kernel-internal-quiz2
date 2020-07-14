COPS = -Wall -Wextra -Werror 

all: xs.o

xs.o: xs.c
	gcc $(COPS) -o xs -std=gnu11 xs.c
