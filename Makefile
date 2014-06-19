CFLAGS=	-O3 -g -Wall
# LDFLAGS= -L/opt/local/lib
# LOADLIBES= -lcunit

all:	count

count:	count.o hash.o

hash.o:	hash.h hash.c

clean:
	rm -rf count *.o *.dSYM a.out *~ 
