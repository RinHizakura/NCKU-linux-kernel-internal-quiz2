COPS = -Wall -Wextra -Werror 

ifeq ("$(NOCOW)","1")
	COPS += -DNOCOW
endif

all: xs.o
clean:
	rm -rf xs
valgrind:
	gcc $(COPS) -o xs -g -std=gnu11 xs.c
	valgrind --leak-check=full --show-leak-kinds=all  ./xs

perf:
	gcc $(COPS) -o xs -g -std=gnu11 xs.c
	sudo perf stat --repeat 1 -e cache-misses,cache-references ./xs

xs.o: xs.c
	gcc $(COPS) -o xs -g -std=gnu11 xs.c
