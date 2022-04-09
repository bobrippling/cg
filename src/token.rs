use crate::enum_string;

#[derive(PartialEq, Eq, Debug)]
pub enum Token {
    Integer(i32),
    Identifier(String),
    Bareword(String),
    String(Box<[u8]>),

    Punctuation(Punctuation),
    Keyword(Keyword),
    Op(Op),
    Cmp(Cmp),
}

enum_string! {
    pub enum Punctuation {
        LParen = "(",
        RParen = ")",
        LBrace = "{",
        RBrace = "}",
        LSquare = "<",
        RSquare = ">",
        Dot = ".",
        Comma = ",",
        Equal = "=",
        Colon = ":",
        Semi = ";",
        Star = "*",
        Arrow = "->",
        Ellipses = "...",
    }
}

enum_string! {
    pub enum Keyword {
        Load = "load",
        Store = "store",
        Alloca = "alloca",
        Elem = "elem",
        Ptradd = "ptradd",
        Ptrsub = "ptrsub",
        Ptr2int = "ptr2int",
        Int2ptr = "int2ptr",
        Ptrcast = "ptrcast",
        Memcpy = "memcpy",
        Ret = "ret",
        Zext = "zext",
        Sext = "sext",
        Trunc = "trunc",
        Br = "br",
        Jmp = "jmp",
        Label = "label",
        Call = "call",
        I1 = "i1",
        I2 = "i2",
        I4 = "i4",
        I8 = "i8",
        F4 = "f4",
        F8 = "f8",
        Flarge = "flarge",
        Void = "void",
        Undef = "undef",
        Global = "global",
        Internal = "internal",
        Aliasinit = "aliasinit",
        Type = "type",
        Asm = "asm",
    }
}

#[derive(PartialEq, Eq, Debug)]
pub enum Op {
    Add,
    Sub,
    Mul,
    Sdiv,
    Smod,
    Udiv,
    Umod,
    Xor,
    Or,
    And,
    Shiftl,
    ShiftRLogic,
    ShiftRArith,
}

#[derive(PartialEq, Eq, Debug)]
pub enum Cmp {
    Eq,
    Ne,
    Gt,
    Ge,
    Lt,
    Le,
}

/*

int token_is_op(enum token t, enum op *const o)
{
    switch(t){
#define KW(t) case tok_ ## t: break;
#define OTHER KW
#define PUNCT(t, c) case tok_ ## t: break;
#define PUNCTSTR(t, c) case tok_ ## t: break;
#define OP(t) case tok_ ## t: *o = op_ ## t; return 1;
#define CMP KW
    TOKENS
#undef CMP
#undef OP
#undef PUNCTSTR
#undef PUNCT
#undef OTHER
#undef KW
    }
    return 0;
}

int token_is_cmp(enum token t, enum op_cmp *const o)
{
    switch(t){
#define KW(t) case tok_ ## t: break;
#define OTHER KW
#define PUNCT(t, c) case tok_ ## t: break;
#define PUNCTSTR(t, c) case tok_ ## t: break;
#define OP(t) case tok_ ## t: break;
#define CMP(t) case tok_ ## t: *o = cmp_ ## t; return 1;
    TOKENS
#undef CMP
#undef OP
#undef PUNCTSTR
#undef PUNCT
#undef OTHER
#undef KW
    }
    return 0;
}
*/