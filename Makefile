OBJ = val.o  main.o mem.o dynarray.o op.o \
      dynmap.o \
      isn.o regalloc_isn.o regalloc_blk.o \
      function.o variable.o global.o block.o unit.o \
      die.o io.o str.o lbl.o \
      tokenise.o parse.o \
      x86.o

#opt_cprop.o opt_storeprop.o opt_dse.o opt_loadmerge.o

HEADERS = backend.h dyn.h dynmap.h \
          isn.h isn_internal.h regalloc_isn.h isn_struct.h \
          mem.h op.h opt_cprop.h \
          val.h val_internal.h val_struct.h \
          block.h block_internal.h block_struct.h regalloc_blk.h \
          x86.h

SRC = ${OBJ:.o=.c}

CFLAGS_DEFINE = -D_POSIX_C_SOURCE=200112L

CFLAGS = -std=c89 -g -Wall -Wextra ${CFLAGS_CONFIGURE} ${CFLAGS_DEFINE}
LDFLAGS = -g

all: tags ir

ir: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

check: ir
	make -Ctest

tags: ${SRC}
	ctags ${SRC} ${HEADERS}

clean:
	make -C test clean
	rm -f ir ${OBJ} ${OBJ:%.o=.%.d}

Makefile.dep: ${SRC} ${HEADERS}
	${CC} -MM ${SRC} > $@

.%.d: %.c
	${CC} -MM ${CFLAGS} $< > $@

-include ${OBJ:%.o=.%.d}
-include Makefile.cfg

.PHONY: clean all check
