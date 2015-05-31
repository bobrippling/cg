.data
_0: .long 0
_1: .long 1
_2: .long 2
_3: .long 3
_4: .long 4
_5: .long 5
_6: .long 6
_7: .long 7
_8: .long 8
_9: .long 9

.text

.globl main
main:
.globl _main
_main:
	push %rbp
	mov %rsp, %rbp
	push %rbx

	call test

	# mov %eax, %ebx
	# mov %eax, %esi
	# lea _l(%rip), %rdi
	# call _printf

	popq %rbx
	leave
	ret

f:
