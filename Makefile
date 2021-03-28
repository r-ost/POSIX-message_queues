CC = gcc
CFLAGS = -std=gnu99 -Wall
LDLIBS = -lrt

all: generator processor

generator: generator.c
	${CC} -o generator ${CFLAGS} generator.c ${LDLIBS}

processor: processor.c
	${CC} -o processor ${CFLAGS} processor.c ${LDLIBS}

.PHONY: clean

clean:
	rm -f generator processor