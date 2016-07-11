OBJ = val.o  main.o mem.o dynarray.o op.o init.o \
      dynmap.o string.o imath.o \
      isn.o \
      function.o variable.o global.o variable_global.o block.o unit.o \
      die.o io.o str.o lbl.o \
      tokenise.o parse.o \
      lifetime.o \
      type.o uniq_type_list.o type_iter.o target.o \
      regset.o \
      pass_abi.o pass_isel.o pass_regalloc.o
      #x86.o x86_call.o x86_isns.o

#opt_cprop.o opt_storeprop.o opt_dse.o opt_loadmerge.o

SRC = ${OBJ:.o=.c}

TEST_OBJ = utests.o type.o uniq_type_list.o type_iter.o \
           mem.o dynmap.o dynarray.o imath.o regset.o

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

# va_list:
mem.o: CFLAGS += -std=c99

utests: ${TEST_OBJ} strbuf/strbuf.a
	@echo link $@
	$Q${CC} -o $@ ${TEST_OBJ} strbuf/strbuf.a ${LDFLAGS}

check-utests: utests
	./utests

check: ir check-utests
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
-include ${TEST_OBJ:%.o=.%.d}
-include Makefile.cfg

STRBUF_PATH = strbuf
include strbuf/strbuf.mk

.PHONY: clean all check
