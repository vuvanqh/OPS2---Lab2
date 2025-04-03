LDLIBS=-lpthread -lm

CC=gcc
CFLAGS= -std=gnu99 -Wall
L_FLAGS=-fsanitize=address,undefined

TARGET=client
FILES=${TARGET}.o

.PHONY: clean all

${TARGET} : ${FILES}
	${CC} ${L_FLAGS} -o ${TARGET} ${FILES}

${TARGET}.o: ${TARGET}.c
	${CC} ${C_FLAGS} -o ${TARGET}.o -c ${TARGET}.c

server: server.c 	
	gcc server.c -fsanitize=address,undefined -std=gnu99 -Wall -o server

all: ${TARGET} server

clean:
	rm -f ${FILES} ${TARGET}