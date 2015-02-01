OBJ = val.o main.o mem.o isn.o op.o \
      dynmap.o opt_cprop.o

SRC = ${OBJ:.o=.c}

CFLAGS = -g -Wall -Wextra
LDFLAGS = -g

all: tags val

val: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

tags: ${SRC}
	ctags ${SRC}

clean:
	rm -f val ${OBJ}

Makefile.dep: ${SRC}
	${CC} -MM ${SRC} > $@

-include Makefile.dep

.PHONY: clean all
