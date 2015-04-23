OBJ = val.o val_allocas.o main.o mem.o op.o \
      dynmap.o opt_cprop.o opt_storeprop.o opt_dse.o \
      opt_loadmerge.o \
      isn.o isn_reg.o \
      function.o block.o blk_reg.o unit.o \
      die.o io.o str.o \
      tokenise.o parse.o \
      x86.o

HEADERS = backend.h dyn.h dynmap.h \
          isn.h isn_internal.h isn_reg.h isn_struct.h \
          mem.h op.h opt_cprop.h \
          val.h val_internal.h val_struct.h \
          block.h block_internal.h block_struct.h blk_reg.h \
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
	rm -f ir ${OBJ}

Makefile.dep: ${SRC} ${HEADERS}
	${CC} -MM ${SRC} > $@

-include Makefile.dep
-include Makefile.cfg

.PHONY: clean all check
