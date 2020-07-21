COPS = -Wall -Wextra -Werror 

all: xs.o
valgrind:
	gcc $(COPS) -o xs -g -std=gnu11 xs.c
	valgrind --leak-check=full --show-leak-kinds=all  ./xs
xs.o: xs.c
	gcc $(COPS) -o xs -g -std=gnu11 xs.c
