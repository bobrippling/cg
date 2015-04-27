.PHONY: clean

harness: harness.o
	${CC} -o $@ $<

harness.o: gen.s
	${CC} -c -o $@ $<

gen.s: src.s harness.s
	cat harness.s > $@
	sed -E 's;([^$$L_0-9])([0-9]+)(,|$$);\1_\2(%rip)\3;' < src.s >> $@

clean:
	rm -f src.s gen.s harness.o harness
