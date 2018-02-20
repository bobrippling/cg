OBJ = val.o mem.o dynarray.o op.o init.o \
      dynmap.o string.o imath.o \
      isn.o backend_isn.o isn_replace.o \
      function.o variable.o global.o variable_global.o block.o unit.o \
      die.o io.o str.o lbl.o \
      tokenise.o parse.o \
      lifetime.o mangle.o builtins.o \
      type.o uniq_type_list.o type_iter.o target.o location.o \
      regset.o regset_marks.o \
      pass_abi.o pass_isel.o \
      regalloc/linear.o regalloc/interval_array.o regalloc/free_regs.o regalloc/intervals.o \
      spill.o stack.o \
      x86.o x86_isel.o x86_call.o x86_isns.o

OBJ_MAIN = ${OBJ} main.o
OBJ_UTEST = utests.o type.o uniq_type_list.o type_iter.o \
           mem.o dynmap.o dynarray.o imath.o regset.o regset_marks.o
OBJ_CTEST = ${OBJ} ctests.o
OBJ_ALL = ${OBJ_MAIN} ctests.o utests.o

#opt_cprop.o opt_storeprop.o opt_dse.o opt_loadmerge.o
#pass_spill.o pass_regalloc.o

SRC = ${OBJ_MAIN:.o=.c}

CFLAGS_DEFINE = -D_POSIX_C_SOURCE=200112L -Istrbuf

CFLAGS = ${CFLAGS_CONFIGURE} ${CFLAGS_DEFINE}
LDFLAGS = ${LDFLAGS_CONFIGURE}

Q = @

all: tags ir check-utests check-ctests

check-%: %
	./$<

check: ir check-utests check-ctests
	make -Ctest

ir: ${OBJ_MAIN} strbuf/strbuf.a
	@echo link $@
	$Q${CC} -o $@ ${OBJ_MAIN} strbuf/strbuf.a ${LDFLAGS}

utests: ${OBJ_UTEST} strbuf/strbuf.a
	@echo link $@
	$Q${CC} -o $@ ${OBJ_UTEST} strbuf/strbuf.a ${LDFLAGS}

ctests: ${OBJ_CTEST}
	@echo link $@
	$Q${CC} -o $@ ${OBJ_CTEST} strbuf/strbuf.a ${LDFLAGS} -ldl

%.o: %.c
	@echo compile $<
	$Q${CC} -c -o $@ $< ${CFLAGS} ${CPPFLAGS}

tags: ${SRC}
	@echo ctags
	$Q-ctags ${SRC} *.h */*.h

clean:
	make -C strbuf clean
	rm -f ir ctests utests ${OBJ_ALL} ${OBJ_ALL:%.o=%.d}

%.d: %.c
	@echo depend $<
	$Q${CC} -MM -MT ${<:.c=.o} ${CFLAGS} $< > $@

# va_copy:
mem.o: CFLAGS += -std=c99

# mkstemp:
io.o: CPPFLAGS += -D_XOPEN_SOURCE=500

-include ${OBJ_ALL:%.o=%.d}
-include Makefile.cfg

STRBUF_PATH = strbuf
include strbuf/strbuf.mk

.PHONY: clean all check
