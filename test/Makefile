IR_TESTS = $(shell echo *.ir)

TEST = ./irtest

ir = ../ir

.PHONY: all

all: ${IR_TESTS:.ir=.test}

%.test: %.ir
	ir=${ir} ${TEST} $<
