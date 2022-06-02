use std::collections::HashMap;
use std::io::Read;
use std::iter;
use std::rc::Rc;

use bitflags::bitflags;
use thiserror::Error;

use crate::block::Block;
use crate::func::{Func, FuncAttr};
use crate::global::Global;
use crate::init::{Init, InitFlags, InitTopLevel, PtrInit};
use crate::isn::Isn;
use crate::srcloc::SrcLoc;
use crate::token::{Keyword, Op, Punctuation, Token};
use crate::ty::{Primitive, Type, TypeQueries, TypeS};
use crate::val::Val;
use crate::variable::Var;
use crate::{
    tokenise::{self, Tokeniser},
    unit::Unit,
};

type PResult<T> = std::result::Result<T, ParseError>;

#[derive(Error, Debug)]
pub enum ParseError {
    #[error("expected {0}, got {1:?}")]
    Expected(&'static str, Token),

    #[error("parse error, expected {0}")]
    Generic(String),

    #[error(transparent)]
    LexError(#[from] tokenise::Error),

    #[error("overflow parsing number")]
    Overflow,

    #[error("unexpected eof")]
    UnexpectedEof,
}

bitflags! {
    #[derive(Default)]
    pub struct ValOpts: u8 {
        const CREATE = 1 << 0;
        const ALLOCA = 1 << 1;
        const LABEL = 1 << 2;
    }
}

pub struct Parser<'scope, R, SemaErr> {
    pub tok: Tokeniser<'scope, R>,
    pub unit: Unit<'scope>,
    pub sema_error: SemaErr,
    pub names2vals: HashMap<String, Rc<Val<'scope>>>,
}

impl<'scope, R, SemaErr> Parser<'scope, R, SemaErr>
where
    R: Read,
    SemaErr: FnMut(String),
{
    pub fn parse(mut self) -> Result<Unit<'scope>, (ParseError, SrcLoc)> {
        match self.parse_no_loc() {
            Ok(()) => Ok(self.unit),
            Err(e) => Err((e, self.tok.loc())),
        }
    }

    fn parse_no_loc(&mut self) -> Result<(), ParseError> {
        while !self.eof()? {
            self.global()?;
        }
        Ok(())
    }

    fn eof(&mut self) -> PResult<bool> {
        Ok({
            if self.tok.eof() {
                true
            } else {
                self.accept(Token::Eof)?
            }
        })
    }

    fn next(&mut self) -> tokenise::LexResult {
        self.tok.next()
    }

    fn expect<T, Check>(&mut self, f: Check) -> PResult<T>
    where
        Check: FnOnce(Token) -> std::result::Result<T, (Token, &'static str)>,
    {
        let tok = self.next()?;
        f(tok).map_err(|(tok, desc)| ParseError::Expected(desc, tok))
    }

    fn eat(&mut self, expected: Token) -> PResult<()> {
        self.expect(|tok| {
            if tok == expected {
                Ok(())
            } else {
                Err((tok, expected.desc()))
            }
        })
    }

    fn expect_integer(&mut self) -> PResult<i32> {
        self.accept_integer()
            .and_then(|i| i.ok_or_else(|| ParseError::Generic("integer expected".into())))
    }

    fn accept(&mut self, expected: Token) -> PResult<bool> {
        let res = self.accept_with(|got| if got == expected { Ok(()) } else { Err(got) })?;

        Ok(match res {
            Some(()) => true,
            None => false,
        })
    }

    fn accept_with<T, F>(&mut self, f: F) -> PResult<Option<T>>
    where
        F: FnOnce(Token) -> Result<T, Token>,
    {
        let got = self.next()?;

        Ok(match f(got) {
            Ok(t) => Some(t),
            Err(tok) => {
                self.tok.unget(tok);
                None
            }
        })
    }

    fn accept_bareword(&mut self) -> PResult<Option<String>> {
        self.accept_with(|tok| {
            if let Token::Bareword(bw) = tok {
                Ok(bw)
            } else {
                Err(tok)
            }
        })
    }

    fn accept_integer(&mut self) -> PResult<Option<i32>> {
        self.accept_with(|tok| {
            if let Token::Integer(n) = tok {
                Ok(n)
            } else {
                Err(tok)
            }
        })
    }

    fn global(&mut self) -> PResult<()> {
        let is_type = self.accept(Token::Keyword(Keyword::Type))?;

        let name = self.expect(|tok| {
            if let Token::Identifier(ident) = tok {
                Ok(ident)
            } else {
                Err((tok, "identifier for global"))
            }
        })?;

        self.eat(Token::Punctuation(Punctuation::Equal))?;

        let (ty, toplvl_args) = self.parse_type_maybe_func()?;

        let new = if is_type {
            self.unit.types.add_alias(&name, ty);
            Global::Type { name, ty }
        } else if matches!(ty, TypeS::Func { .. }) {
            Global::Func(self.parse_function(name, ty, toplvl_args)?)
        } else {
            Global::Var(self.parse_variable(name, ty)?)
        };

        let old = self.unit.globals.add(new);
        if let Some(old) = old {
            (self.sema_error)(format!("global '{}' already defined", old.name()));
        }

        Ok(())
    }

    fn parse_type_maybe_func<'s>(&mut self) -> PResult<(Type<'scope>, Vec<String>)> {
        let (ty, toplvl_args) = self.parse_type_maybe_func_nochk()?;

        if ty.array_elem().is_fn() {
            (self.sema_error)("type is an array of functions".into());
        }

        if ty.called().array_elem().is_some() {
            (self.sema_error)("type is a function returning an array".into());
        }

        Ok((ty, toplvl_args))
    }

    fn parse_type_maybe_func_nochk(&mut self) -> PResult<(Type<'scope>, Vec<String>)> {
        /*
         * void
         * i1, i2, i4, i8
         * f4, f8, flarge
         * { i2, f4 }
         * [ i2 x 7 ]
         * i8 *
         * f4 (i2)
         * $typename
         */
        let mut arg_names = vec![];

        let t = match self.next()? {
            Token::Identifier(spel) => match self.unit.types.resolve_alias(&spel) {
                Some(ty) => ty,
                None => {
                    (self.sema_error)(format!("no such type '{}'", spel));
                    self.unit.types.void()
                }
            },

            Token::Keyword(kw) => match kw {
                Keyword::I1 => self.unit.types.primitive(Primitive::I1),
                Keyword::I2 => self.unit.types.primitive(Primitive::I2),
                Keyword::I4 => self.unit.types.primitive(Primitive::I4),
                Keyword::I8 => self.unit.types.primitive(Primitive::I8),
                Keyword::F4 => self.unit.types.primitive(Primitive::F4),
                Keyword::F8 => self.unit.types.primitive(Primitive::F8),
                Keyword::FLarge => self.unit.types.primitive(Primitive::FLarge),
                Keyword::Void => self.unit.types.void(),
                _ => return Err(ParseError::Generic("type expected".into())),
            },

            Token::Punctuation(Punctuation::LBrace) => {
                let (types, variadic, names) =
                    self.parse_type_list(Token::Punctuation(Punctuation::RBrace), false)?;

                assert!(names.is_empty());

                if variadic {
                    return Err(ParseError::Generic("structs can't be variadic".into()));
                }

                self.unit.types.struct_of(types)
            }

            Token::Punctuation(Punctuation::LSquare) => {
                let elemty = self.parse_type()?;

                let mul = self.expect(|tok| {
                    if let Token::Bareword(ident) = tok {
                        Ok(ident)
                    } else {
                        Err((tok, "\"x\" for array multiplier"))
                    }
                })?;

                if mul != "x" {
                    return Err(ParseError::Generic(format!(
                        "'x' expected for array multiplier, got {}",
                        mul
                    )));
                }

                let nelems = self.expect_integer()?;
                let nelems = nelems.try_into().map_err(|_| ParseError::Overflow)?;

                let t = self.unit.types.array_of(elemty, nelems);

                self.eat(Token::Punctuation(Punctuation::RSquare))?;

                t
            }

            tok => {
                return Err(ParseError::Generic(format!("type expected, got {}", tok)));
            }
        };

        let mut t = t;
        loop {
            if self.accept(Token::Punctuation(Punctuation::Star))? {
                t = self.unit.types.ptr_to(t);
                continue;
            }

            if self.accept(Token::Punctuation(Punctuation::LParen))? {
                let (types, variadic, names) =
                    self.parse_type_list(Token::Punctuation(Punctuation::RParen), true)?;

                if !names.is_empty() {
                    let old = arg_names;
                    arg_names = names;
                    if !old.is_empty() {
                        (self.sema_error)("multiple top-level argument names".into());
                    }
                }

                t = self.unit.types.func_of(t, types, variadic);
                continue;
            }

            break;
        }

        Ok((t, arg_names))
    }

    fn parse_type_list(
        &mut self,
        closer: Token,
        toplvl_args: bool,
    ) -> PResult<(Vec<Type<'scope>>, bool, Vec<String>)> {
        let mut types = vec![];
        let mut variadic = false;
        let mut names = vec![];

        if self.accept(Token::Punctuation(Punctuation::Ellipses))? {
            variadic = true;
        } else {
            let mut have_idents = false;
            let mut seen_closer = false;

            loop {
                if self.accept(closer.clone())? {
                    seen_closer = true;
                    break;
                }
                let mut memb = self.parse_type()?;

                if memb.is_fn() {
                    (self.sema_error)("function in aggregate".into());
                    memb = self.unit.types.void();
                }

                let mut skip_comma = false;
                if toplvl_args {
                    if types.is_empty() {
                        // first time, decide whether to have arg names
                        let maybe_ident = self.next()?;

                        match maybe_ident {
                            Token::Identifier(id) => {
                                have_idents = true;
                                names.push(id);
                            }
                            Token::Punctuation(Punctuation::Comma) => {
                                have_idents = false;
                                skip_comma = true;
                            }
                            tok => {
                                return Err(ParseError::Generic(format!(
                                    "expected identifier or comma, got {}",
                                    tok
                                )));
                            }
                        }
                    } else if have_idents {
                        let id = self.expect(|tok| {
                            if let Token::Identifier(id) = tok {
                                Ok(id)
                            } else {
                                Err((tok, "identifier for argument name"))
                            }
                        })?;
                        names.push(id);
                    }
                }

                types.push(memb);

                if skip_comma {
                    continue;
                } else if self.accept(Token::Punctuation(Punctuation::Comma))? {
                    if self.accept(Token::Punctuation(Punctuation::Ellipses))? {
                        variadic = true;
                    } else {
                        /* , xyz */
                        continue;
                    }
                }
                break;
            }

            if !seen_closer {
                self.eat(closer)?;
            }
        }

        Ok((types, variadic, names))
    }

    fn parse_type(&mut self) -> PResult<Type<'scope>> {
        let (ty, _args) = self.parse_type_maybe_func()?;
        Ok(ty)
    }

    fn parse_function<'s>(
        &'s mut self,
        name: String,
        ty: Type<'scope>,
        toplvl_args: Vec<String>,
    ) -> PResult<Func<'scope>>
    where
        'scope: 's,
    {
        let mut f: Func = Func::new(name, ty, self.unit.target, toplvl_args, self.unit.blk_arena);
        let mut attr = FuncAttr::default();

        // look for weak (and other attributes)
        loop {
            if self.accept(Token::Keyword(Keyword::Internal))? {
                attr |= FuncAttr::INTERNAL;
            } else if let Some(bareword) = self.accept_bareword()? {
                if bareword == "weak" {
                    attr |= FuncAttr::WEAK;
                } else {
                    return Err(ParseError::Generic(format!(
                        "unknown function modifier '{}'",
                        bareword
                    )));
                }
            } else {
                break;
            }
        }
        f.add_attr(attr);

        if self.accept(Token::Punctuation(Punctuation::LBrace))? {
            let mut cur_blk: &Block = f.get_entry();

            loop {
                if self.eof()? {
                    return Err(ParseError::Generic(format!(
                        "No closing bracket for function body"
                    )));
                }
                if self.accept(Token::Punctuation(Punctuation::RBrace))? {
                    break;
                }

                self.parse_block(&mut f, &mut cur_blk)?;
            }
        }

        // TODO: function_finalize(fn);
        // TODO: p->names2vals = NULL;
        Ok(f)
    }

    fn parse_variable(&mut self, name: String, ty: Type<'scope>) -> PResult<Var<'scope>> {
        let mut flags = InitFlags::default();

        if self.accept(Token::Keyword(Keyword::Global))? {
            // ok
        } else if self.accept(Token::Keyword(Keyword::Internal))? {
            flags |= InitFlags::INTERNAL;
        } else {
            return Ok(Var {
                name: name.into(),
                ty,
                init: None,
            });
        }

        while let Some(bareword) = self.accept_bareword()? {
            match bareword.as_str() {
                "const" => flags |= InitFlags::CONSTANT,
                "weak" => flags |= InitFlags::WEAK,
                other => {
                    return Err(ParseError::Generic(format!(
                        "unknown variable modifier '{}'",
                        other
                    )));
                }
            }
        }

        let init = self.parse_init(ty)?;
        let init = InitTopLevel { init, flags };

        Ok(Var {
            name: name.into(),
            ty,
            init: Some(init),
        })
    }

    fn parse_elem_inits(
        &mut self,
        types: impl Iterator<Item = Type<'scope>>,
    ) -> PResult<Vec<Init<'scope>>> {
        self.eat(Token::Punctuation(Punctuation::LBrace))?;

        let mut elem_inits = vec![];
        let mut seen_rbrace = false;

        for ty in types {
            let elem = self.parse_init(ty)?;
            elem_inits.push(elem);

            if self.accept(Token::Punctuation(Punctuation::RBrace))? {
                seen_rbrace = true;
                break;
            }

            self.eat(Token::Punctuation(Punctuation::Comma))?;

            if self.accept(Token::Punctuation(Punctuation::RBrace))? {
                seen_rbrace = true;
                break;
            }

            if self.eof()? {
                return Err(ParseError::UnexpectedEof);
            }
        }

        if !seen_rbrace {
            self.eat(Token::Punctuation(Punctuation::RBrace))?;
        }

        Ok(elem_inits)
    }

    fn parse_init(&mut self, ty: Type<'scope>) -> PResult<Init<'scope>> {
        if self.accept(Token::Keyword(Keyword::Aliasinit))? {
            /* aliasinit <type> <init>
             * (useful for unions) */
            let as_ = self.parse_type()?;

            if as_.size() > ty.size() {
                (self.sema_error)("aliasinit type size > actual type size".into());
            }

            return Ok(Init::Alias {
                as_,
                init: Box::new(self.parse_init(as_)?),
            });
        }

        let init = match ty.resolve() {
            TypeS::Void | TypeS::Alias { .. } => unimplemented!(),
            TypeS::Func { .. } => unreachable!(),

            TypeS::Ptr { pointee, sz: _ } => Init::Ptr(self.parse_init_ptr(pointee)?),
            TypeS::Primitive(_) => {
                let i = self.expect_integer()?;

                Init::Int(i.try_into().unwrap())
            }
            &TypeS::Array { elem, n } => {
                let string = self.accept_with(|tok| {
                    if let Token::String(s) = tok {
                        Ok(s)
                    } else {
                        Err(tok)
                    }
                })?;

                match string {
                    Some(s) => {
                        if !matches!(elem.as_primitive(), Some(Primitive::I1)) {
                            (self.sema_error)("string init not an i1 array".into());
                        }

                        Init::Str(s.into())
                    }
                    None => {
                        let elem_inits = self.parse_elem_inits(iter::repeat(elem))?;

                        /* zero-sized arrays aren't specially handled here */
                        if n != elem_inits.len() {
                            (self.sema_error)(format!(
                                "init count mismatch: {} vs {}",
                                n,
                                elem_inits.len()
                            ));
                        }

                        Init::Array(elem_inits)
                    }
                }
            }
            TypeS::Struct { membs } => {
                let elem_inits = self.parse_elem_inits(membs.iter().copied())?;

                if membs.len() != elem_inits.len() {
                    (self.sema_error)(format!(
                        "init count mismatch: {} vs {}",
                        membs.len(),
                        elem_inits.len()
                    ));
                }

                Init::Struct(elem_inits)
            }
        };

        Ok(init)
    }

    fn parse_init_ptr(&mut self, ty: Type<'scope>) -> PResult<PtrInit> {
        Ok(match self.next()? {
            Token::Identifier(label) => {
                let lookup_global = || match self.unit.globals.by_name(&label) {
                    Some(glob) => {
                        return Ok(glob.ty());
                    }
                    None => {
                        return Err(ParseError::Generic(format!(
                            "no such (global) identifier \"{}\"",
                            label
                        )))
                    }
                };
                let ty_global = lookup_global()?;

                let mul = self.accept_with(|tok| {
                    Ok(match tok {
                        Token::Op(Op::Add) => 1,
                        Token::Op(Op::Sub) => -1,
                        _ => return Err(tok),
                    })
                })?;

                let offset = if let Some(mul) = mul {
                    let i = self.expect_integer()?;
                    i * mul
                } else {
                    0
                };

                let is_anyptr = if let Some(bw) = self.accept_bareword()? {
                    if bw == "anyptr" {
                        true
                    } else {
                        return Err(ParseError::Generic(format!("unexpected \"{}\"", bw)));
                    }
                } else {
                    false
                };

                if !is_anyptr && ty != ty_global {
                    (self.sema_error)(format!(
                        "initialisation type mismatch: init {:?} with {:?}",
                        ty, ty_global
                    ));
                }

                PtrInit::Label {
                    label,
                    offset: offset as _,
                    is_anyptr,
                }
            }

            Token::Integer(i) => PtrInit::Int(i as usize),

            tok => {
                return Err(ParseError::Generic(format!(
                    "pointer initialiser expected, got {}",
                    tok
                )));
            }
        })
    }

    fn parse_block(
        &mut self,
        func: &mut Func<'scope>,
        block: &mut &'scope Block<'scope>,
    ) -> PResult<()> {
        match self.tok.next()? {
            Token::Eof => {}

            Token::Keyword(Keyword::Ret) => self.parse_ret(func, block)?,

            Token::Identifier(ident) => {
                if self.accept(Token::Punctuation(Punctuation::Colon))? {
                    let from = *block;

                    let (this_block, created) = func.get_block(ident);
                    *block = this_block;

                    if !created && !this_block.is_tenative() {
                        (self.sema_error)(format!(
                            "block '{}' already exists",
                            this_block
                                .label()
                                .as_ref()
                                .expect("entry block can't appear here")
                        ));
                    }

                    if from.is_unknown_ending() {
                        /* current block is fall-thru */
                        from.set_jmp(this_block);
                    }
                } else {
                    self.parse_ident(ident)?;
                }
            }

            Token::Keyword(Keyword::Jmp) => self.parse_jmp()?,
            Token::Keyword(Keyword::Br) => self.parse_br()?,
            Token::Keyword(Keyword::Store) => self.parse_store()?,
            Token::Keyword(Keyword::Label) => self.parse_label()?,
            Token::Keyword(Keyword::Asm) => self.parse_asm()?,
            Token::Keyword(Keyword::Memcpy) => self.parse_memcpy()?,

            tok => {
                return Err(ParseError::Generic(format!("unexpected token {}", tok)));
            }
        }

        Ok(())
    }

    fn parse_ret(
        &mut self,
        func: &mut Func<'scope>,
        block: &mut &'scope Block<'scope>,
    ) -> PResult<()> {
        let expected_ty = func.ty().called().unwrap();
        let v = self.parse_val(func)?;

        if !v.ty().can_return_to(expected_ty) {
            (self.sema_error)(format!(
                "mismatching return type (returning {:?} to {:?})",
                v.ty(),
                expected_ty
            ));
        }

        block.add_isn(Isn::Ret(v));
        block.set_exit();

        Ok(())
    }

    fn parse_ident(&mut self, _ident: String) -> PResult<()> {
        todo!()
    }
    fn parse_jmp(&mut self) -> PResult<()> {
        todo!()
    }
    fn parse_br(&mut self) -> PResult<()> {
        todo!()
    }
    fn parse_store(&mut self) -> PResult<()> {
        todo!()
    }
    fn parse_label(&mut self) -> PResult<()> {
        todo!()
    }
    fn parse_asm(&mut self) -> PResult<()> {
        todo!()
    }
    fn parse_memcpy(&mut self) -> PResult<()> {
        todo!()
    }

    fn parse_val(&mut self, func: &mut Func<'scope>) -> PResult<Rc<Val<'scope>>> {
        if let Some(ident) = self.accept_with(|tok| {
            if let Token::Identifier(ident) = tok {
                Ok(ident)
            } else {
                Err(tok)
            }
        })? {
            if self.unit.types.resolve_alias(&ident).is_some() {
                // we're at the beginning of a type, not an identifier
            } else {
                return Ok(self.uniq_val(func, ident, None, ValOpts::default())?);
            }
        }

        // need a type and a literal, e.g. i32 5
        let ty = self.parse_type()?;

        let v = if ty.is_void() {
            Val::new_void(self.unit.types.void())
        } else if let Some(n) = self.accept_integer()? {
            Val::new_i(n, ty)
        } else if self.accept(Token::Keyword(Keyword::Undef))? {
            Val::new_undef(ty)
        } else {
            return Err(ParseError::Generic("value operand expected".into()));
        };

        Ok(Rc::new(v))
    }

    fn uniq_val(
        &mut self,
        func: &mut Func<'scope>,
        name: String,
        ty: Option<Type<'scope>>,
        opts: ValOpts,
    ) -> PResult<Rc<Val<'scope>>> {
        if ty.is_some() {
            assert!(opts.contains(ValOpts::CREATE));
        } else {
            assert!(!opts.contains(ValOpts::CREATE));
        }

        let mut emit_existing_check = || {
            if opts.contains(ValOpts::CREATE) {
                (self.sema_error)(format!("pre-existing identifier '{}'", name));
            }
        };

        if let Some(v) = self.names2vals.get(&name) {
            emit_existing_check();
            return Ok(Rc::clone(v));
        }

        if let Some((idx, arg_ty)) = func.arg_by_name(&name) {
            emit_existing_check();

            if let Some(ty) = ty {
                assert_eq!(arg_ty, ty);
            }

            let v = Val::new_argument(name.clone(), arg_ty);

            let v = self.map_val(name, v);
            func.register_arg_val(idx, Rc::clone(&v));

            return Ok(v);
        }

        if let Some(glob) = self.unit.globals.by_name(&name) {
            emit_existing_check();
            let v = Rc::new(Val::new_global(&mut self.unit.types, glob));
            return Ok(v);
        }

        let ty = if opts.contains(ValOpts::CREATE) {
            ty.expect("CREATE without ty")
        } else {
            return Err(ParseError::Generic(format!(
                "undeclared identifier '{}'",
                name
            )));
        };

        let v = if opts.contains(ValOpts::LABEL) {
            Val::new_label(name.clone(), ty)
        } else {
            Val::new_local(name.clone(), ty, opts.contains(ValOpts::ALLOCA))
        };

        Ok(self.map_val(name, v))
    }

    fn map_val(&mut self, name: String, v: Val<'scope>) -> Rc<Val<'scope>> {
        Rc::clone(self.names2vals.entry(name).or_insert_with(|| Rc::new(v)))
    }
}

#[cfg(test)]
mod test {
    use std::cell::Cell;

    use typed_arena::Arena;

    use crate::{
        blk_arena::BlkArena, block::BlockKind, init::PtrInit, target::Target, val::Location,
    };

    use super::*;

    type Parser<'scope> = super::Parser<'scope, &'scope [u8], &'scope mut dyn FnMut(String)>;

    fn parse_str<F>(s: &[u8], f: F)
    where
        F: FnOnce(Parser, &mut dyn FnMut(&mut Parser)),
    {
        let target = Target::dummy();
        let ty_arena = Arena::new();
        let blk_arena = BlkArena::new();

        let error = Cell::new(None);
        let parser = Parser {
            tok: Tokeniser::new(s, "fname"),
            unit: Unit::new(&target, &ty_arena, &blk_arena),
            sema_error: (&mut |s| error.set(Some(s))) as _,
            names2vals: Default::default(),
        };

        let mut done = false;
        f(parser, &mut |parser| {
            if let Some(e) = error.take() {
                panic!("sema error during parse: {}", e);
            }

            parser.eat(Token::Eof).unwrap(); // needed to bump us onto eof()
            assert!(parser.eof().unwrap());

            done = true;
        });
        assert!(done);
    }

    fn with_unit<F>(s: &[u8], f: F)
    where
        F: FnOnce(Unit),
    {
        let target = Target::dummy();
        let ty_arena = Arena::new();
        let blk_arena = BlkArena::new();

        let error = Cell::new(None);
        let parser = Parser {
            tok: Tokeniser::new(s, "fname"),
            unit: Unit::new(&target, &ty_arena, &blk_arena),
            sema_error: (&mut |s| error.set(Some(s))) as _,
            names2vals: Default::default(),
        };

        let unit = parser.parse().unwrap();

        if let Some(e) = error.take() {
            panic!("sema error during parse: {}", e);
        }

        f(unit)
    }

    #[test]
    fn parse_type() {
        parse_str(b"i4", |mut parser, done| {
            let t = parser.parse_type().unwrap();
            done(&mut parser);

            assert_eq!(t, parser.unit.types.primitive(Primitive::I4));
            assert!(matches!(t, TypeS::Primitive(Primitive::I4)));
        })
    }

    #[test]
    fn parse_alias() {
        parse_str(b"$size_t", |mut parser, done| {
            let i4 = parser.unit.types.primitive(Primitive::I4);
            parser.unit.types.add_alias("size_t", i4);

            let t = parser.parse_type().unwrap();
            done(&mut parser);

            assert_eq!(t, i4); // resolve(), etc
            match t {
                &TypeS::Alias { ref name, actual } => {
                    assert_eq!(name, "size_t");
                    assert_eq!(actual, i4);
                }
                got => panic!("expected Alias, got {:?}", got),
            }
        })
    }

    #[test]
    fn parse_empty_func() {
        parse_str(b"internal {}", |mut parser, done| {
            let types = &mut parser.unit.types;
            let i4 = types.primitive(Primitive::I4);
            let i1 = types.primitive(Primitive::I1);
            let fnty = types.func_of(i4, vec![i1, i4], false);

            let arg_names = vec!["arg1".into(), "arg2".into()];
            let f = parser
                .parse_function("f".into(), fnty, arg_names.clone())
                .unwrap();
            done(&mut parser);

            assert_eq!(f.mangled_name(), None);
            assert_eq!(f.ty(), fnty);
            assert_eq!(f.arg_names(), arg_names);
            assert_eq!(f.attr(), FuncAttr::INTERNAL);
            assert_eq!(f.name(), "f");
        });
    }

    #[test]
    fn parse_func() {
        parse_str(b"internal { ret $arg2 }", |mut parser, done| {
            let types = &mut parser.unit.types;
            let i4 = types.primitive(Primitive::I4);
            let i1 = types.primitive(Primitive::I1);
            let fnty = types.func_of(i4, vec![i1, i4], false);

            let arg_names = vec!["arg1".into(), "arg2".into()];
            let f = parser
                .parse_function("f".into(), fnty, arg_names.clone())
                .unwrap();
            done(&mut parser);

            assert_eq!(f.mangled_name(), None);
            assert_eq!(f.ty(), fnty);
            assert_eq!(f.arg_names(), arg_names);
            assert_eq!(f.attr(), FuncAttr::INTERNAL);
            assert_eq!(f.name(), "f");

            assert_eq!(f.arg_by_name("arg3"), None);
            assert_eq!(f.arg_by_name("arg1"), Some((0, i1)));
            assert_eq!(f.arg_by_name("arg2"), Some((1, i4)));

            let block = f.entry().unwrap();
            assert_eq!(*block.label(), None);
            assert!(matches!(*block.kind(), BlockKind::EntryExit));

            match block.isns()[..] {
                [Isn::Ret(ref retval)] => {
                    let val = retval.as_ref();

                    assert_eq!(val.ty(), i4);
                    assert_eq!(val.location(), Some(&Location));
                }
                _ => panic!(),
            }
        });
    }

    #[test]
    fn parse_variable() {
        parse_str(b"global const weak 5", |mut parser, done| {
            let i4 = parser.unit.types.primitive(Primitive::I4);
            let v = parser.parse_variable("var1".into(), i4).unwrap();
            done(&mut parser);

            assert_eq!(&v.name, "var1");
            assert_eq!(v.ty, i4);
            let init = v.init.unwrap();
            assert_eq!(init.flags, InitFlags::CONSTANT | InitFlags::WEAK);
            assert!(matches!(init.init, Init::Int(5)));
        });

        parse_str(b"", |mut parser, done| {
            let i4 = parser.unit.types.primitive(Primitive::I4);
            let v = parser.parse_variable("var1".into(), i4).unwrap();
            done(&mut parser);

            assert_eq!(&v.name, "var1");
            assert_eq!(v.ty, i4);
            assert_eq!(v.init, None);
        });
    }

    #[test]
    fn parse_aggregate_init() {
        parse_str(b"internal { 1, 2 }", |mut parser, done| {
            let i1 = parser.unit.types.primitive(Primitive::I1);
            let i4 = parser.unit.types.primitive(Primitive::I4);
            let struct_ty = parser.unit.types.struct_of(vec![i4, i1]);

            let v = parser.parse_variable("var1".into(), struct_ty).unwrap();
            done(&mut parser);

            assert_eq!(&v.name, "var1");
            assert_eq!(v.ty, struct_ty);
            let init = v.init.unwrap();
            assert_eq!(init.flags, InitFlags::INTERNAL);
            assert_eq!(init.init, Init::Struct(vec![Init::Int(1), Init::Int(2)]));
        });

        parse_str(b"global { 1, 2, 3 }", |mut parser, done| {
            let i4 = parser.unit.types.primitive(Primitive::I4);
            let array_ty = parser.unit.types.array_of(i4, 3);

            let v = parser.parse_variable("var1".into(), array_ty).unwrap();
            done(&mut parser);

            assert_eq!(&v.name, "var1");
            assert_eq!(v.ty, array_ty);
            let init = v.init.unwrap();
            assert_eq!(init.flags, InitFlags::default());
            assert_eq!(
                init.init,
                Init::Array(vec![Init::Int(1), Init::Int(2), Init::Int(3)])
            );
        });
    }

    #[test]
    fn parse_ptr_init() {
        parse_str(b"8", |mut parser, done| {
            let i4 = parser.unit.types.primitive(Primitive::I4);
            let ptr_ty = parser.unit.types.ptr_to(i4);

            let init = parser.parse_init(ptr_ty).unwrap();
            done(&mut parser);

            assert_eq!(init, Init::Ptr(PtrInit::Int(8)));
        });

        parse_str(b"$label", |mut parser, done| {
            let i8 = parser.unit.types.primitive(Primitive::I8);
            let ptr_ty = parser.unit.types.ptr_to(i8);

            let old = parser.unit.globals.add(Global::Var(Var {
                name: "label".into(),
                ty: i8,
                init: Some(InitTopLevel {
                    init: Init::Int(0), // dummy
                    flags: Default::default(),
                }),
            }));
            assert!(old.is_none());

            let init = parser.parse_init(ptr_ty).unwrap();
            done(&mut parser);

            assert_eq!(
                init,
                Init::Ptr(PtrInit::Label {
                    label: "label".into(),
                    offset: 0,
                    is_anyptr: false,
                })
            );
        });

        parse_str(b"$label add 7", |mut parser, done| {
            let i8 = parser.unit.types.primitive(Primitive::I8);
            let ptr_ty = parser.unit.types.ptr_to(i8);

            let old = parser.unit.globals.add(Global::Var(Var {
                name: "label".into(),
                ty: i8,
                init: Some(InitTopLevel {
                    init: Init::Int(0), // dummy
                    flags: Default::default(),
                }),
            }));
            assert!(old.is_none());

            let init = parser.parse_init(ptr_ty).unwrap();
            done(&mut parser);

            assert_eq!(
                init,
                Init::Ptr(PtrInit::Label {
                    label: "label".into(),
                    offset: 7,
                    is_anyptr: false,
                })
            );
        });

        parse_str(b"$label sub 1", |mut parser, done| {
            let i8 = parser.unit.types.primitive(Primitive::I8);
            let ptr_ty = parser.unit.types.ptr_to(i8);

            let old = parser.unit.globals.add(Global::Var(Var {
                name: "label".into(),
                ty: i8,
                init: Some(InitTopLevel {
                    init: Init::Int(0), // dummy
                    flags: Default::default(),
                }),
            }));
            assert!(old.is_none());

            let init = parser.parse_init(ptr_ty).unwrap();
            done(&mut parser);

            assert_eq!(
                init,
                Init::Ptr(PtrInit::Label {
                    label: "label".into(),
                    offset: -1,
                    is_anyptr: false,
                })
            );
        });

        parse_str(b"$label add 13 anyptr", |mut parser, done| {
            let i8 = parser.unit.types.primitive(Primitive::I8);
            let ptr_ty = parser.unit.types.ptr_to(i8);

            let old = parser.unit.globals.add(Global::Var(Var {
                name: "label".into(),
                ty: i8,
                init: Some(InitTopLevel {
                    init: Init::Int(0), // dummy
                    flags: Default::default(),
                }),
            }));
            assert!(old.is_none());

            let init = parser.parse_init(ptr_ty).unwrap();
            done(&mut parser);

            assert_eq!(
                init,
                Init::Ptr(PtrInit::Label {
                    label: "label".into(),
                    offset: 13,
                    is_anyptr: true,
                })
            );
        });
    }

    #[test]
    fn parse_alias_init() {
        parse_str(b"aliasinit i2 3", |mut parser, done| {
            let i4 = parser.unit.types.primitive(Primitive::I4);

            let init = parser.parse_init(i4).unwrap();
            done(&mut parser);

            assert_eq!(
                init,
                Init::Alias {
                    as_: parser.unit.types.primitive(Primitive::I2),
                    init: Box::new(Init::Int(3)),
                }
            );
        });
    }

    #[test]
    fn parse_init_with_types() {
        let s = b"
            $lbl = i1 internal 3
            $y = { i1* } internal { $lbl }
            type $t = { i4, { i1* }* }
            $x = $t internal { 3, $y }
        ";

        with_unit(s, |mut unit| {
            let types = &mut unit.types;
            let globals = &mut unit.globals;

            let i4 = types.primitive(Primitive::I4);
            let i1 = types.primitive(Primitive::I1);
            let i1p = types.ptr_to(i1);
            let i1p_struct = types.struct_of(vec![i1p]);
            let i1p_structp = types.ptr_to(i1p_struct);
            let t_ty = types.struct_of(vec![i4, i1p_structp]);

            match globals.by_name("lbl").unwrap() {
                Global::Var(v) => assert_eq!(
                    *v,
                    Var {
                        name: "lbl".into(),
                        ty: types.primitive(Primitive::I1),
                        init: Some(InitTopLevel {
                            init: Init::Int(3),
                            flags: InitFlags::INTERNAL,
                        }),
                    }
                ),
                _ => unreachable!(),
            }

            match globals.by_name("y").unwrap() {
                Global::Var(v) => assert_eq!(
                    *v,
                    Var {
                        name: "y".into(),
                        ty: i1p_struct,
                        init: Some(InitTopLevel {
                            init: Init::Struct(vec![Init::Ptr(PtrInit::Label {
                                label: "lbl".into(),
                                offset: 0,
                                is_anyptr: false,
                            })]),
                            flags: InitFlags::INTERNAL,
                        }),
                    }
                ),
                _ => unreachable!(),
            }

            match globals.by_name("t").unwrap() {
                &Global::Type { ref name, ty } => {
                    assert_eq!(name, "t");
                    assert_eq!(ty, t_ty);
                }
                _ => unreachable!(),
            }

            match globals.by_name("x").unwrap() {
                Global::Var(v) => assert_eq!(
                    *v,
                    Var {
                        name: "x".into(),
                        ty: t_ty,
                        init: Some(InitTopLevel {
                            flags: InitFlags::INTERNAL,
                            init: Init::Struct(vec![
                                Init::Int(3),
                                Init::Ptr(PtrInit::Label {
                                    label: "y".into(),
                                    offset: 0,
                                    is_anyptr: false,
                                })
                            ]),
                        }),
                    }
                ),
                _ => unreachable!(),
            }

            assert_eq!(globals.len(), 4);
        })
    }
}

/*
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <assert.h>

#include "mem.h"

#include "block.h"
#include "tokenise.h"

#include "parse.h"

#include "dynmap.h"
#include "val.h"
#include "isn.h"
#include "isn_struct.h"
#include "type.h"
#include "variable_struct.h"

#define ISN(kind, ...) parsed_isn(isn_##kind(__VA_ARGS__))

typedef struct {
    tokeniser *tok;
    unit *unit;
    function *func;
    block *entry;
    dynmap *names2vals;
    int err;

    parse_error_fn *error_v;
    void *error_ctx;
} parse;

enum val_opts
{
    VAL_CREATE = 1 << 0,
    VAL_ALLOCA = 1 << 1,
    VAL_LABEL = 1 << 2,
};

static type *parse_type(parse *);

attr_printf(2, 0)
static void error_v(parse *p, const char *fmt, va_list l)
{
    char buf[32];
    size_t off;

    if(p->error_v){
        p->error_v(token_curfile(p->tok), token_curlineno(p->tok), p->error_ctx, fmt, l);
        return;
    }

    fprintf(stderr, "%s:%u: ", token_curfile(p->tok), token_curlineno(p->tok));

    vfprintf(stderr, fmt, l);
    fputc('\n', stderr);

    token_curline(p->tok, buf, sizeof buf, &off);
    fprintf(stderr, "at: '%s'\n", buf);
    fprintf(stderr, "     ");

    fprintf(stderr, "%*c^\n", (int)off, ' ');

    p->err = 1;
}

attr_printf(2, 3)
static void parse_error(parse *p, const char *fmt, ...)
{
    va_list l;

    va_start(l, fmt);
    error_v(p, fmt, l);
    va_end(l);
}

attr_printf(2, 3)
static void sema_error(parse *p, const char *fmt, ...)
{
    va_list l;

    va_start(l, fmt);
    error_v(p, fmt, l);
    va_end(l);
}

static type *default_type(parse *p)
{
    return type_get_primitive(unit_uniqtypes(p->unit), i4);
}

static isn *parsed_isn(isn *i)
{
    i->compiler_generated = false;
    return i;
}

static void create_names2vals(parse *p)
{
    if(p->names2vals)
        return;

    p->names2vals = dynmap_new(
            const char *, (dynmap_cmp_f *)strcmp, dynmap_strhash);
}

static val *map_val(parse *p, char *name, val *v)
{
    val *old;

    create_names2vals(p);

    old = dynmap_set(char *, val *, p->names2vals, name, v);
    assert(!old);

    return v;
}

static void eat(parse *p, const char *desc, enum token expect)
{
    enum token got = token_next(p->tok);
    if(got == expect)
        return;

    parse_error(p, "expected %s%s%s, got %s",
            token_to_str(expect),
            desc ? " for " : "",
            desc ? desc : "",
            token_to_str(got));
}

static int parse_finished(tokeniser *tok)
{
    return token_peek(tok) == tok_eof || token_peek(tok) == tok_unknown;
}


static void parse_call(parse *p, char *ident)
{
    val *target;
    val *into;
    dynarray args = DYNARRAY_INIT;
    dynarray *argtys;
    bool variadic = false;
    bool tyerror = false;
    type *retty = NULL;
    bool stret;
    size_t i;

    target = parse_val(p);

    type *ptr = type_deref(val_type(target));
    if(ptr)
        retty = type_func_call(ptr, &argtys, &variadic);

    if(!retty){
        retty = default_type(p);
        tyerror = true; /* disable argument type checks */
        sema_error(p, "call requires function (pointer) operand (got %s)",
                type_to_str(val_type(target)));
    }

    stret = type_is_struct(retty);
    if(stret){
        retty = type_get_ptr(unit_uniqtypes(p->unit), retty);
    }

    into = uniq_val(p, ident, retty, VAL_CREATE | (stret ? VAL_ALLOCA : 0));
    /* void results are fine */

    eat(p, "call paren", tok_lparen);

    for(i = 0; ; i++){
        val *arg;

        if(dynarray_is_empty(&args)){
            if(token_peek(p->tok) == tok_rparen)
                break; /* call x() */
        }else{
            eat(p, "call comma", tok_comma);
        }

        arg = parse_val(p);

        dynarray_add(&args, arg);

        if(!tyerror){
            if(i < dynarray_count(argtys)){
                type *argty = dynarray_ent(argtys, i);

                if(argty != val_type(arg)){
                    char buf[256];

                    sema_error(p, "argument %zu mismatch (%s passed to %s)",
                            i + 1,
                            type_to_str_r(buf, sizeof buf, val_type(arg)),
                            type_to_str(argty));
                }
            }else if(variadic){
                /* fine */
            }else{
                sema_error(p, "too many arguments to function");
            }
        }

        if(token_peek(p->tok) == tok_rparen || parse_finished(p->tok))
            break;
    }

    if(i + 1 < dynarray_count(argtys)){
        sema_error(p, "too few arguments to function");
    }

    eat(p, "call paren", tok_rparen);

    block_add_isn(p->entry, ISN(call, into, target, &args));

    dynarray_reset(&args);
}

static bool cmp_types_valid(enum op_cmp cmp, type *a, type *b)
{
    return type_eq(a, b);
}

static bool op_types_valid(enum op op, type *a, type *b)
{
    return (type_is_int(a) || type_is_float(a, true)) && type_eq(a, b);
}

static void parse_ident(parse *p, char *spel)
{
    enum token tok;

    eat(p, "assignment", tok_equal);

    tok = token_next(p->tok);

    switch(tok){
        case tok_load:
        {
            val *rhs = parse_val(p);
            type *deref_ty = type_deref(val_type(rhs));
            val *lhs;

            if(!deref_ty){
                sema_error(p, "load operand not a pointer type");
                deref_ty = default_type(p);
            }
            if(type_array_element(deref_ty)){
                sema_error(p, "load operand is (pointer-to) array type");
                deref_ty = default_type(p);
            }

            lhs = uniq_val(p, spel, deref_ty, VAL_CREATE);
            block_add_isn(p->entry, ISN(load, lhs, rhs));
            break;
        }

        case tok_alloca:
        {
            val *vlhs;
            type *ty = parse_type(p);

            if(type_is_fn(ty)){
                sema_error(p, "alloca of function type");
                ty = default_type(p);
            }

            vlhs = uniq_val(
                    p, spel,
                    type_get_ptr(unit_uniqtypes(p->unit), ty),
                    VAL_CREATE | VAL_ALLOCA);

            block_add_isn(p->entry, ISN(alloca, vlhs));
            break;
        }

        case tok_elem:
        {
            val *vlhs;
            val *index_into;
            val *idx;
            type *array_ty, *element_ty, *resolved_ty;
            struct uniq_type_list *uniqtypes = unit_uniqtypes(p->unit);

            index_into = parse_val(p);

            eat(p, "elem", tok_comma);

            idx = parse_val(p);

            /* given a pointer to an array type,
             * return a pointer to element `idx' */

            array_ty = type_deref(val_type(index_into));

            if(!array_ty){
                sema_error(p, "elem requires pointer type");

                array_ty = type_get_array(
                        uniqtypes,
                        default_type(p),
                        1);
            }

            element_ty = type_array_element(array_ty);
            if(!element_ty && type_is_struct(array_ty)){
                size_t i;
                if(val_is_int(idx, &i)){
                    element_ty = type_struct_element(array_ty, i);

                    if(!element_ty){
                        sema_error(p, "elem index out of struct bounds");
                        element_ty = default_type(p);
                    }
                }
            }

            if(!element_ty){
                sema_error(p, "elem requires (pointer to) array/struct type");
                element_ty = default_type(p);
            }

            if(type_size(val_type(idx)) != type_size(type_get_sizet(unit_uniqtypes(p->unit)))){
                sema_error(p, "elem (array-based) requires pointer-sized integer type (rhs)");
            }

            resolved_ty = type_get_ptr(uniqtypes, element_ty);

            vlhs = uniq_val(p, spel, resolved_ty, VAL_CREATE);

            block_add_isn(p->entry, ISN(elem, index_into, idx, vlhs));
            break;
        }

        case tok_ptradd:
        case tok_ptrsub:
        {
            val *vlhs, *vrhs, *vout;

            vlhs = parse_val(p);

            eat(p, "ptradd/sub-comma", tok_comma);

            vrhs = parse_val(p);

            if(!type_deref(val_type(vlhs))){
                sema_error(p, "ptradd/sub requires pointer type (lhs)");
            }
            if(tok == tok_ptradd){
                if(!type_is_int(val_type(vrhs))){
                    sema_error(p, "ptradd requires integer type (rhs)");
                }
                if(type_size(val_type(vrhs)) != type_size(type_get_sizet(unit_uniqtypes(p->unit)))){
                    sema_error(p, "ptradd requires pointer-sized integer type (rhs)");
                }
            }else{
                if(!type_eq(val_type(vlhs), val_type(vrhs))){
                    sema_error(p, "ptrsub type mismatch");
                }
            }
            if(type_is_void(type_deref(val_type(vlhs)))){
                sema_error(p, "can't increment/decrement void*");
            }

            vout = uniq_val(
                    p,
                    spel,
                    tok == tok_ptradd
                    ? val_type(vlhs)
                    : type_get_sizet(unit_uniqtypes(p->unit)),
                    VAL_CREATE);

            block_add_isn(p->entry,
                    (tok == tok_ptradd
                    ? isn_ptradd
                    : isn_ptrsub)(vlhs, vrhs, vout));
            break;
        }

        case tok_zext:
        case tok_sext:
        case tok_trunc:
        {
            val *from;
            val *vres;
            type *ty_to;
            isn *(*isn_make)(val *from, val *to);
            unsigned sz_from, sz_to;
            int extend = 1;

            ty_to = parse_type(p);

            if(!type_is_int(ty_to)){
                sema_error(p, "ext/trunc requires integer type");
                ty_to = type_get_primitive(unit_uniqtypes(p->unit), iMAX);
            }

            eat(p, "ext/trunc", tok_comma);

            from = parse_val(p);

            if(!type_is_int(val_type(from))){
                sema_error(p, "ext/trunc argument requires integer type");
            }

            vres = uniq_val(p, spel, ty_to, VAL_CREATE);

            sz_from = type_size(val_type(from));
            sz_to = type_size(ty_to);

            switch(tok){
                case tok_sext:
                    isn_make = isn_sext;
                    break;
                case tok_zext:
                    isn_make = isn_zext;
                    break;
                case tok_trunc:
                    isn_make = isn_trunc;
                    extend = 0;
                    break;
                default:
                    assert(0 && "unreachable");
            }

            if(!(extend ? sz_from < sz_to : sz_from > sz_to)){
                sema_error(
                        p,
                        "%s has incorrect operand sizes, from=%d, to=%d",
                        extend ? "extend" : "truncate",
                        sz_from,
                        sz_to);
            }

            block_add_isn(p->entry, ISN(make, from, vres));
            break;
        }

        case tok_call:
        {
            parse_call(p, spel);
            break;
        }

        default:
        {
            int is_cmp = 0;
            enum op op;
            enum op_cmp cmp;

            if(token_is_op(tok, &op) || (is_cmp = 1, token_is_cmp(tok, &cmp))){
                /* x = add a, b */
                val *vlhs = parse_val(p);
                val *vrhs = (eat(p, "operator", tok_comma), parse_val(p));
                val *vres;
                type *opty;
                uniq_type_list *utl = unit_uniqtypes(p->unit);

                if(is_cmp){
                    if(!cmp_types_valid(cmp, val_type(vlhs), val_type(vrhs))){
                        sema_error(p, "mismatching types in cmp");
                    }
                }else{
                    switch(op){
                        default:
                            if(!op_types_valid(op, val_type(vlhs), val_type(vrhs))){
                                sema_error(p, "invalid types for op");
                            }
                            break;
                        case op_shiftl:
                        case op_shiftr_logic:
                        case op_shiftr_arith:
                            break;
                    }
                }

                if(is_cmp){
                    opty = type_get_primitive(utl, i1);
                }else if(tok == tok_ptrsub){
                    opty = type_get_sizet(utl);
                }else{
                    opty = val_type(vlhs);
                }

                vres = uniq_val(p, spel, opty, VAL_CREATE);

                if(is_cmp)
                    block_add_isn(p->entry, ISN(cmp, cmp, vlhs, vrhs, vres));
                else
                    block_add_isn(p->entry, ISN(op, op, vlhs, vrhs, vres));

            }else if(tok == tok_ident){
                char *from = token_last_ident(p->tok);
                val *lhs, *rhs;

                rhs = uniq_val(p, from, NULL, 0);
                lhs = uniq_val(p, spel, val_type(rhs), VAL_CREATE);

                block_add_isn(p->entry, ISN(copy, lhs, rhs));

            }else{
                parse_error(p, "expected load, alloca, elem or operator (got %s)",
                        token_to_str(tok));
            }
            break;
        }

        case tok_ptr2int:
        case tok_int2ptr:
        case tok_ptrcast:
        {
            type *to = parse_type(p);
            val *input;
            val *vres;

            if(tok == tok_ptr2int ? type_is_int(to) : !!type_deref(to)){
                /* fine */
            }else{
                sema_error(p, "%s type expected for %s",
                        tok == tok_int2ptr ? "integer" : "pointer",
                        token_to_str(tok));
            }

            eat(p, "comma", tok_comma);

            input = parse_val(p);

            vres = uniq_val(p, spel, to, VAL_CREATE);

            if(tok == tok_ptr2int)
                block_add_isn(p->entry, isn_ptr2int(input, vres));
            else if(tok == tok_int2ptr)
                block_add_isn(p->entry, isn_int2ptr(input, vres));
            else if(tok == tok_ptrcast)
                block_add_isn(p->entry, ISN(ptrcast, input, vres));
            else
                assert(0 && "unreachable");
            break;
        }
    }
}

static bool permitted_to_return(type *expr, type *to)
{
    if(type_is_struct(to)){
        return type_eq(type_deref(expr), to);
    }else{
        return type_eq(expr, to);
    }
}

static void parse_ret(parse *p)
{
    type *expected_ty = type_func_call(function_type(p->func), NULL, NULL);
    val *v = parse_val(p);

    if(!permitted_to_return(val_type(v), expected_ty)){
        char buf[256];

        sema_error(p, "mismatching return type (returning %s to %s)",
                type_to_str(val_type(v)),
                type_to_str_r(buf, sizeof buf, expected_ty));
    }

    block_add_isn(p->entry, ISN(ret, v));
    block_set_type(p->entry, BLK_EXIT);
}

static void parse_store(parse *p)
{
    val *lval;
    val *rval;

    lval = parse_val(p);
    eat(p, "store comma", tok_comma);
    rval = parse_val(p);

    if(!type_eq(type_deref(val_type(lval)), val_type(rval))){
        char buf[256];

        sema_error(p, "store type mismatch (storing %s to %s)",
                type_to_str(val_type(rval)),
                type_to_str_r(buf, sizeof buf, val_type(lval)));
    }

    block_add_isn(p->entry, ISN(store, rval, lval));
}

static void enter_unreachable_code(parse *p)
{
    p->entry = NULL;
}

static void parse_br(parse *p)
{
    /* br cond, ltrue, lfalse */
    char *ltrue, *lfalse;
    block *btrue, *bfalse;
    val *cond = parse_val(p);

    if(!type_is_primitive(val_type(cond), i1)){
        sema_error(p, "br requires 'i1' condition (got %s)",
                type_to_str(val_type(cond)));
    }

    eat(p, "br comma", tok_comma);
    eat(p, "br true", tok_ident);
    ltrue = token_last_ident(p->tok);

    eat(p, "br comma", tok_comma);
    eat(p, "br false", tok_ident);
    lfalse = token_last_ident(p->tok);

    btrue = function_block_find(p->func, p->unit, ltrue, NULL);
    bfalse = function_block_find(p->func, p->unit, lfalse, NULL);

    block_add_isn(p->entry, ISN(br, cond, btrue, bfalse));
    block_set_branch(p->entry, cond, btrue, bfalse);

    enter_unreachable_code(p);
}

static void parse_jmp(parse *p)
{
    if(token_accept(p->tok, tok_star)){
        val *target = parse_val(p);

        block_add_isn(p->entry, ISN(jmp_computed, target));
        block_set_type(p->entry, BLK_JMP_COMP);

    }else{
        block *target;
        char *lbl;

        eat(p, "jmp label", tok_ident);
        lbl = token_last_ident(p->tok);

        target = function_block_find(p->func, p->unit, lbl, NULL);

        block_add_isn(p->entry, ISN(jmp, target));
        block_set_jmp(p->entry, target);
    }

    enter_unreachable_code(p);
}

static void parse_label(parse *p)
{
    char *lbl;
    uniq_type_list *utl = unit_uniqtypes(p->unit);
    type *blkty = type_get_ptr(utl, type_get_void(utl));
    val *blkval;

    eat(p, "label decl", tok_ident);
    lbl = token_last_ident(p->tok);

    blkval = uniq_val(p, xstrdup(lbl), blkty, VAL_CREATE | VAL_LABEL);
    block_add_isn(p->entry, ISN(label, blkval));

    function_block_find(p->func, p->unit, lbl, NULL);
}

static void parse_asm(parse *p)
{
    struct string str;

    eat(p, "asm string", tok_string);

    token_last_string(p->tok, &str);

    block_add_isn(p->entry, ISN(asm, &str));
}

static void parse_memcpy(parse *p)
{
    val *dest, *src;

    dest = parse_val(p);
    eat(p, "memcpy", tok_comma);
    src = parse_val(p);

    if(!type_eq(val_type(dest), val_type(src))){
        sema_error(p, "mismatching memcpy types");
    }else if(!type_deref(val_type(dest))){
        sema_error(p, "memcpy type is not a pointer");
    }

    block_add_isn(p->entry, ISN(memcpy, dest, src));
}
*/
