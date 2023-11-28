CC=gcc
CXX=g++
NAME=farmbound-cli
CFLAGS=-Wall -ggdb -O3
LDFLAGS=-lm

all : $(NAME)

$(NAME).o : $(NAME).c
	$(CC) -c $(CFLAGS) -o $@ $<

$(NAME) : $(NAME).o
	$(CC) -o $@ $^ ${LDFLAGS}

memtest : $(NAME)
	valgrind -v --show-reachable=yes --leak-check=full ./$^

clean :
	-rm -f $(NAME) *.o

install : $(NAME)
	install -d $(DESTDIR)/usr/local/bin
	install $(NAME) $(DESTDIR)/usr/local/bin
