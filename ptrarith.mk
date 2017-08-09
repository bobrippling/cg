ptrarith: ptrarith_call.c ptrarith.o
	cc -o ptrarith ptrarith.o ptrarith_call.c

ptrarith.o: ptrarith.s
	as $< -o $@

ptrarith.s: ptrarith.ir
	./ir $< -o $@

clean:
	rm -f ptrarith.o ptrarith.s ptrarith

.PHONY: clean
