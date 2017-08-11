all: isel
	./$<

isel: isel_call.c isel.s
	${CC} -o $@ $^

isel.s: isel.ir ir
	./ir $< >$@.tmp
	mv $@.tmp $@

.PHONY: clean all

clean:
	rm -f isel isel.s
