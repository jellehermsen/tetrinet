######## Configuration area

# Your C compiler
CC = cc

# The passed compilation flags
CFLAGS = -O2 -I/usr/include/ncurses -g -Wall -fno-builtin-log

# Whether to enable IPv6 support
IPV6 = 1

# Whether to have builtin server in the tetrinet client (available through
# -server argument) (tetrinet-server will be built always regardless this)
# BUILTIN_SERVER = 1

# If you experience random delays and server freezes when accepting new
# clients, enable this.
# NO_BRUTE_FORCE_DECRYPTION = 1


######## End of configuration area


OBJS = sockets.o tetrinet.o tetris.o tty.o

ifdef IPV6
	CFLAGS += -DHAVE_IPV6
endif
ifdef BUILTIN_SERVER
	CFLAGS += -DBUILTIN_SERVER
	OBJS += server.o
endif
ifdef NO_BRUTE_FORCE_DECRYPTION
	CFLAGS += -DNO_BRUTE_FORCE_DECRYPTION
endif


########


all: tetrinet tetrinet-server

install: all
	cp -p tetrinet tetrinet-server /usr/games

clean:
	rm -f tetrinet tetrinet-server *.o

spotless: clean

binonly:
	rm -f *.[cho] Makefile
	rm -rf CVS/


########


tetrinet: $(OBJS)
	$(CC) -o $@ $(OBJS) -lncurses

tetrinet-server: server.c sockets.c tetrinet.c tetris.c server.h sockets.h tetrinet.h tetris.h
	$(CC) $(CFLAGS) -o $@ -DSERVER_ONLY server.c sockets.c tetrinet.c tetris.c

.c.o:
	$(CC) $(CFLAGS) -c $<

server.o:	server.c tetrinet.h tetris.h server.h sockets.h
sockets.o:	sockets.c sockets.h tetrinet.h
tetrinet.o:	tetrinet.c tetrinet.h io.h server.h sockets.h tetris.h
tetris.o:	tetris.c tetris.h tetrinet.h io.h sockets.h
tty.o:		tty.c tetrinet.h tetris.h io.h

tetrinet.h:	io.h
