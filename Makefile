OBJ = val.o  main.o mem.o dynarray.o op.o \
      dynmap.o \
      strbuf_fixed.o \
      isn.o regalloc.o \
      function.o variable.o global.o block.o unit.o \
      die.o io.o str.o lbl.o \
      tokenise.o parse.o \
      type.o \
      x86.o

#opt_cprop.o opt_storeprop.o opt_dse.o opt_loadmerge.o

HEADERS = backend.h dyn.h dynmap.h \
          isn.h isn_internal.h regalloc.h isn_struct.h \
          mem.h op.h opt_cprop.h \
          val.h val_internal.h val_struct.h \
          block.h block_internal.h block_struct.h \
          x86.h

SRC = ${OBJ:.o=.c}

CFLAGS_DEFINE = -D_POSIX_C_SOURCE=200112L

CFLAGS = ${CFLAGS_CONFIGURE} ${CFLAGS_DEFINE}
LDFLAGS = -g ${LDFLAGS_CONFIGURE}

all: tags ir

ir: ${OBJ}
	@echo link $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

%.o: %.c
	@echo compile $<
	@${CC} -c -o $@ $< ${CFLAGS}

check: ir
	make -Ctest

tags: ${SRC}
	@echo ctags
	@ctags ${SRC} ${HEADERS}

clean:
	make -C test clean
	rm -f ir ${OBJ} ${OBJ:%.o=.%.d}

Makefile.dep: ${SRC} ${HEADERS}
	${CC} -MM ${SRC} > $@

.%.d: %.c
	@echo depend $<
	@${CC} -MM ${CFLAGS} $< > $@

-include ${OBJ:%.o=.%.d}
-include Makefile.cfg

.PHONY: clean all check
