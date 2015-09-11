.PHONY: clean

harness: harness.o
	${CC} -o $@ $<

harness.o: gen.s
	${CC} -c -o $@ $<

gen.s: src.s harness.s
	cat harness.s > $@.tmp
	cat src.s >> $@.tmp
	mv $@.tmp $@

clean:
	rm -f src.s gen.s harness.o harness
