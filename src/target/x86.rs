// use reg::{Class, Reg};

use crate::regset::RegSet;
use super::Abi;

pub static ABI: Abi = Abi {
    scratch_regs: RegSet{},
    /*
    regt_make(0, 0), /* eax */
    regt_make(2, 0), /* ecx */
    regt_make(3, 0), /* edx */
    regt_make(4, 0), /* esi */
    regt_make(5, 0)  /* edi */
    */
    ret_regs: RegSet{},
    /*
    regt_make(0, 0), /* rax */
    regt_make(3, 0)  /* rdx */
    */
    arg_regs: RegSet{},
    /*
    regt_make(4, 0), /* rdi */
    regt_make(5, 0), /* rsi */
    regt_make(3, 0), /* rdx */
    regt_make(2, 0)  /* rcx */
    // TODO: r8, r9
    */
    callee_saves: RegSet{},
    /*
    regt_make(1, 0) /* rbx */
    */
};
