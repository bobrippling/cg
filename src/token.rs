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

#[derive(PartialEq, Eq, Debug)]
pub enum Punctuation {
    LParen,
    RParen,
    LBrace,
    RBrace,
    LSquare,
    RSquare,
    Dot,
    Comma,
    Equal,
    Colon,
    Semi,
    Star,
    Arrow,
    Ellipses,
}

#[derive(PartialEq, Eq, Debug, Clone, Copy)]
pub enum Keyword {
    Load,
    Store,
    Alloca,
    Elem,
    Ptradd,
    Ptrsub,
    Ptr2int,
    Int2ptr,
    Ptrcast,
    Memcpy,
    Ret,
    Zext,
    Sext,
    Trunc,
    Br,
    Jmp,
    Label,
    Call,
    I1,
    I2,
    I4,
    I8,
    F4,
    F8,
    Flarge,
    Void,
    Undef,
    Global,
    Internal,
    Aliasinit,
    Type,
    Asm,
}

static KEYWORDS: &[(&str, Keyword)] = &[
    ("load", Keyword::Load),
    ("store", Keyword::Store),
    ("alloca", Keyword::Alloca),
    ("elem", Keyword::Elem),
    ("ptradd", Keyword::Ptradd),
    ("ptrsub", Keyword::Ptrsub),
    ("ptr2int", Keyword::Ptr2int),
    ("int2ptr", Keyword::Int2ptr),
    ("ptrcast", Keyword::Ptrcast),
    ("memcpy", Keyword::Memcpy),
    ("ret", Keyword::Ret),
    ("zext", Keyword::Zext),
    ("sext", Keyword::Sext),
    ("trunc", Keyword::Trunc),
    ("br", Keyword::Br),
    ("jmp", Keyword::Jmp),
    ("label", Keyword::Label),
    ("call", Keyword::Call),
    ("i1", Keyword::I1),
    ("i2", Keyword::I2),
    ("i4", Keyword::I4),
    ("i8", Keyword::I8),
    ("f4", Keyword::F4),
    ("f8", Keyword::F8),
    ("flarge", Keyword::Flarge),
    ("void", Keyword::Void),
    ("undef", Keyword::Undef),
    ("global", Keyword::Global),
    ("internal", Keyword::Internal),
    ("aliasinit", Keyword::Aliasinit),
    ("type", Keyword::Type),
    ("asm", Keyword::Asm),
];

impl Keyword {
    pub fn len(&self) -> usize {
        for (s, kw) in KEYWORDS {
            if kw == self {
                return s.len();
            }
        }
        unreachable!()
    }
}

impl TryFrom<&str> for Keyword {
    type Error = ();

    fn try_from(value: &str) -> Result<Self, Self::Error> {
        for (s, kw) in KEYWORDS {
            if value.starts_with(s) {
                return Ok(*kw);
            }
        }

        Err(())
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
