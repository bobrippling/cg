OBJ = val.o main.o mem.o op.o \
      dynmap.o opt_cprop.o opt_storeprop.o opt_dse.o \
      isn.o isn_reg.o \
      x86.o

HEADERS = backend.h dyn.h dynmap.h \
          isn.h isn_internal.h isn_reg.h isn_struct.h \
          mem.h op.h opt_cprop.h \
          val.h val_internal.h val_struct.h \
          x86.h

SRC = ${OBJ:.o=.c}

CFLAGS = -std=c89 -g -Wall -Wextra ${CFLAGS_CONFIGURE}
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
-include Makefile.cfg

.PHONY: clean all
