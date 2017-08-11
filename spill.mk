all: spill
	./$<

spill: spill.s spill_call.o
	${CC} -o $@ $^

spill_call.o: spill_call.s
	${AS} -o $@ $<

spill_call.s: spill_call.ir ir
	./ir $< >$@.tmp
	mv $@.tmp $@

spill_call.ir: spill_call.c
	../ucc/_ir/ucc -w -emit=ir -S -o $@ $<

spill.s: spill.ir ir
	./ir $< >$@.tmp
	mv $@.tmp $@

clean:
	rm -f spill spill_call.s spill_call.ir spill.s

.PHONY: clean all
