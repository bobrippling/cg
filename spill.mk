UCC_IR = ../ucc/_ir/ucc

all: spill
	./$<

spill: spill.s spill_call.s
	${CC} -o $@ $^

spill_call.s: spill_call.ir ir
	./ir $< >$@.tmp
	mv $@.tmp $@

spill_call.ir: spill_call.c
	${UCC_IR} -w -emit=ir -S -o $@ $<

spill.s: spill.ir ir
	./ir $< >$@.tmp
	mv $@.tmp $@

clean:
	rm -f spill spill_call.s spill_call.ir spill.s

.PHONY: clean all
