CFLAGS=-Wall -Wextra -g -fsanitize=address
CC=gcc

battalica:	battallica.c
	$(CC) ${CFLAGS} -o battallica \
	`pkg-config --cflags gtk+-2.0` \
	`pkg-config --libs gtk+-2.0` \
	`pkg-config --libs gthread-2.0` \
	battallica.c -lm

clean:
	rm -f battallica
