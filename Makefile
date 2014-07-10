CFLAGS=	-O3 -g -Wall
# LDFLAGS= -L/opt/local/lib
# LOADLIBES= -lcunit

all:	count lossy_counting

count:	hash.o count.o

lossy_counting: hash.o lossy_counting.o

lossy_counting.o: lossy_counting.h lossy_counting.c

hash.o:	hash.h hash.c

clean:
	rm -rf count lossy_counting *.o *.dSYM a.out *.zip *~
