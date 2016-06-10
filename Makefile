OBJ = val.o  main.o mem.o dynarray.o op.o init.o \
      dynmap.o string.o imath.o \
      isn.o \
      function.o variable.o global.o variable_global.o block.o unit.o \
      die.o io.o str.o lbl.o \
      tokenise.o parse.o \
      type.o target.o \
      pass_abi.o pass_isel.o pass_regalloc.o
      #regalloc.o
      #x86.o x86_call.o x86_isns.o

#opt_cprop.o opt_storeprop.o opt_dse.o opt_loadmerge.o

SRC = ${OBJ:.o=.c}

CFLAGS_DEFINE = -D_POSIX_C_SOURCE=200112L -Istrbuf

CFLAGS = ${CFLAGS_CONFIGURE} ${CFLAGS_DEFINE}
LDFLAGS = ${LDFLAGS_CONFIGURE}

Q = @

all: tags ir

ir: ${OBJ} strbuf/strbuf.a
	@echo link $@
	$Q${CC} -o $@ ${OBJ} strbuf/strbuf.a ${LDFLAGS}

%.o: %.c
	@echo compile $<
	$Q${CC} -c -o $@ $< ${CFLAGS}

check: ir
	make -Ctest

tags: ${SRC}
	@echo ctags
	$Q-ctags ${SRC} *.h

clean:
	make -C test clean
	make -C strbuf clean
	rm -f ir ${OBJ} ${OBJ:%.o=.%.d}

.%.d: %.c
	@echo depend $<
	$Q${CC} -MM ${CFLAGS} $< > $@

-include ${OBJ:%.o=.%.d}
-include Makefile.cfg

STRBUF_PATH = strbuf
include strbuf/strbuf.mk

.PHONY: clean all check
