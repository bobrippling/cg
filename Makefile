OBJ = val.o main.o mem.o isn.o op.o \
      dynmap.o opt_cprop.o \
      x86.o

HEADERS = backend.h dyn.h dynmap.h \
          isn.h isn_internal.h isn_struct.h \
          mem.h op.h opt_cprop.h \
          val.h val_internal.h val_struct.h \
          x86.h

SRC = ${OBJ:.o=.c}

CFLAGS = -g -Wall -Wextra
LDFLAGS = -g

all: tags val

val: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

tags: ${SRC}
	ctags ${SRC} ${HEADERS}

clean:
	rm -f val ${OBJ}

Makefile.dep: ${SRC}
	${CC} -MM ${SRC} > $@

-include Makefile.dep

.PHONY: clean all
