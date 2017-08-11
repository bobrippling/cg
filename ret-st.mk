all: ret-st
	./$<

ret-st: ret-st_call.c ret-st.s
	${CC} -g -o $@ $^

ret-st.s: ret-st.ir ir
	./ir $< >$@.tmp
	mv $@.tmp $@

.PHONY: clean all

clean:
	rm -f ret-st ret-st.s
