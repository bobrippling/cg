OBJ = strbuf_fixed.o
LIB = strbuf.a
TEST_OBJ = test.o ${LIB}

check: test
	./$<

test: ${TEST_OBJ}
	${CC} -o $@ ${TEST_OBJ} ${LDFLAGS}

${LIB}: ${OBJ}
	ar rc $@ ${OBJ}

.%.d: %.c
	${CC} -MM ${CFLAGS} $< > $@

clean:
	rm -f ${OBJ} ${LIB} ${OBJ:%.o=.%.d} ${TEST_OBJ} test

-include ${OBJ:%.o=.%.d}

.PHONY: clean check
