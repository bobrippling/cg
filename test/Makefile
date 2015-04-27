IR_TESTS = $(shell echo *.ir)

TEST = ./irtest

ir = ../ir

.PHONY: all clean

all: ${IR_TESTS:.ir=.test}

clean:
	make -C harness clean

%.test: %.ir
	ir=${ir} ${TEST} $<
