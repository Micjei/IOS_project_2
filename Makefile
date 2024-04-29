CC=gcc
CFLAGS=-Wall -std=gnu99 -Wextra -Werror -pedantic

LDFLAGS=-pthread -lrt

proj2: proj2.c
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

clean:
	rm -f proj2
