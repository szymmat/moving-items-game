FILENAME=prog1b
CC=gcc
CFLAGS= -std=gnu99 -Wall
LDLIBS= -lpthread -lm
all: ${FILENAME}
${FILENAME}: ${FILENAME}.c
	${CC} ${CFLAGS} -o ${FILENAME} ${FILENAME}.c
.PHONY: clean
clean:
	rm ${FILENAME}
