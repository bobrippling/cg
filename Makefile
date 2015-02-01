OBJ = val.o main.o mem.o isn.o op.o \
      dynmap.o

SRC = ${OBJ:.o=.c}

CFLAGS = -g
LDFLAGS = -g

all: val tags

val: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

tags: ${SRC}
	ctags ${SRC}

clean:
	rm -f val ${OBJ}

.PHONY: clean all
