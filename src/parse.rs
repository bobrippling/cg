use std::collections::HashMap;
use std::io::Read;
use std::rc::Rc;

use bitflags::bitflags;
use thiserror::Error;

use crate::block::Block;
use crate::func::{Func, FuncAttr};
use crate::global::Global;
use crate::init::{Init, InitFlags, InitTopLevel};
use crate::isn::Isn;
use crate::srcloc::SrcLoc;
use crate::token::{Keyword, Punctuation, Token};
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
    #[error("{1:?}: expected {0:?}")]
    Expected(Token, SrcLoc),

    #[error("parse error, expected {0}")]
    Generic(String),

    #[error(transparent)]
    LexError(#[from] tokenise::Error),

    #[error("overflow parsing number")]
    Overflow,
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
    pub fn parse(mut self) -> PResult<Unit<'scope>> {
        while !self.eof() {
            self.global()?;
        }

        Ok(self.unit)
    }

    fn eof(&self) -> bool {
        self.tok.eof()
    }

    fn next(&mut self) -> tokenise::LexResult {
        self.tok.next()
    }

    fn expect<T, Check>(&mut self, f: Check) -> PResult<T>
    where
        Check: FnOnce(Token) -> std::result::Result<T, Token>,
    {
        let tok = self.next()?;
        f(tok).map_err(|tok| ParseError::Expected(tok, self.tok.loc()))
    }

    fn eat(&mut self, expected: Token) -> PResult<()> {
        self.expect(|tok| if tok == expected { Ok(()) } else { Err(tok) })
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

    fn global(&mut self) -> PResult<()> {
        let is_type = self.accept(Token::Keyword(Keyword::Type))?;

        let name = self.expect(|tok| {
            if let Token::Identifier(ident) = tok {
                Ok(ident)
            } else {
                Err(tok)
            }
        })?;

        self.eat(Token::Punctuation(Punctuation::Equal))?;

        let (ty, toplvl_args) = self.parse_type_maybe_func()?;

        let new = if is_type {
            Global::Type { name, ty }
        } else if matches!(ty, TypeS::Func { .. }) {
            Global::Func(self.parse_function(name, ty, toplvl_args)?)
        } else {
            Global::Var(self.parse_variable(name, ty)?)
        };

        let (old, _new) = self.unit.globals.add(new);
        if let Some(old) = old {
            (self.sema_error)(format!("global '{}' already defined", old.name()));
        }

        Ok(())
    }

    fn parse_type_maybe_func<'s>(&mut self) -> PResult<(Type<'scope>, Vec<String>)> {
        let (ty, toplvl_args) = self.parse_type_maybe_func_nochk()?;

        if ty.array_elem().is_fn() {
            todo!("error");
            // sema_error(p, "array of functions");
            // return default_type(p);
        }

        if ty.called().array_elem().is_some() {
            todo!("error");
            // sema_error(p, "function returning array");
            // return default_type(p);
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
                    self.unit.types.default()
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
                    todo!("error");
                }

                self.unit.types.struct_of(types)
            }

            Token::Punctuation(Punctuation::LSquare) => {
                let elemty = self.parse_type()?;

                let mul = self.expect(|tok| {
                    if let Token::Bareword(ident) = tok {
                        Ok(ident)
                    } else {
                        Err(tok)
                    }
                })?;

                if mul != "x" {
                    return Err(ParseError::Generic(format!(
                        "'x' expected for array multiplier, got {}",
                        mul
                    )));
                }

                let nelems = self.expect(|tok| {
                    if let Token::Integer(i) = tok {
                        Ok(i)
                    } else {
                        Err(tok)
                    }
                })?;

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
                        todo!("error: multiple top-level argument names")
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
        } else if self.accept(closer)? {
            // done
        } else {
            let mut have_idents = false;

            loop {
                let mut memb = self.parse_type()?;

                if memb.is_fn() {
                    (self.sema_error)("function in aggregate".into());
                    memb = self.unit.types.default();
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
                                Err(tok)
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
        let mut f: Func = Func::new(name, ty, toplvl_args, self.unit.blk_arena);
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
            let mut cur_blk: &Block = self.unit.blk_arena.blks.alloc(Block::new_entry());
            f.set_entry(cur_blk);

            loop {
                if self.eof() {
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

        Ok(Var { name, ty, init })
    }

    fn parse_init(&mut self, ty: Type<'scope>) -> PResult<Init<'scope>> {
        todo!()
        /*
        struct init *init;
        type *subty;

        init = xmalloc(sizeof *init);

        if(token_accept(p->tok, tok_aliasinit)){
            /* aliasinit <type> <init>
             * (useful for unions) */
            init->type = init_alias;
            init->u.alias.as = parse_type(p);

            if(type_size(init->u.alias.as) > type_size(ty))
                sema_error(p, "aliasinit type size > actual type size");

            init->u.alias.init = parse_init(p, init->u.alias.as);

            return init;
        }

        if((subty = type_array_element(ty))){
            const bool is_string = token_accept(p->tok, tok_string);

            if(!is_string)
                eat(p, "array init open brace", tok_lbrace);

            if(is_string){
                struct string str;
                type *elem = type_array_element(ty);

                token_last_string(p->tok, &str);

                if(!elem || !type_is_primitive(elem, i1)){
                    sema_error(p, "init not an i1 array");
                }

                init->type = init_str;
                init->u.str = str;
            }else{
                size_t array_count;

                init->type = init_array;
                dynarray_init(&init->u.elem_inits);

                while(!token_accept(p->tok, tok_eof)){
                    struct init *elem = parse_init(p, subty);

                    dynarray_add(&init->u.elem_inits, elem);

                    if(token_accept(p->tok, tok_rbrace))
                        break;

                    eat(p, "init comma", tok_comma);

                    /* trailing comma: */
                    if(token_accept(p->tok, tok_rbrace))
                        break;
                }

                /* zero-sized arrays aren't specially handled here */
                array_count = type_array_count(ty);
                if(array_count != dynarray_count(&init->u.elem_inits)){
                    sema_error(p, "init count mismatch: %ld vs %ld",
                            (long)array_count, (long)dynarray_count(&init->u.elem_inits));
                }
            }

        }else if(type_is_struct(ty)){
            size_t i = 0;

            init->type = init_struct;
            dynarray_init(&init->u.elem_inits);

            eat(p, "init open brace", tok_lbrace);

            for(; !token_accept(p->tok, tok_eof); i++){
                struct init *elem;

                subty = type_struct_element(ty, i);
                if(!subty){
                    parse_error(p, "excess struct init");
                    break;
                }
                elem = parse_init(p, subty);

                dynarray_add(&init->u.elem_inits, elem);

                if(token_accept(p->tok, tok_rbrace))
                    break;

                eat(p, "init comma", tok_comma);

                /* trailing comma: */
                if(token_accept(p->tok, tok_rbrace))
                    break;
            }

            if(type_struct_element(ty, i + 1))
                parse_error(p, "too few members for struct init");

        }else if(type_deref(ty)){
            parse_init_ptr(p, ty, init);

        }else{
            /* number */
            eat(p, "int initialiser", tok_int);

            init->type = init_int;
            init->u.i = token_last_int(p->tok);
        }

        return init;
        */
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
                        from.add_isn(Isn::Jmp(this_block));
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
        } else if let Some(n) = self.accept_with(|tok| {
            if let Token::Integer(n) = tok {
                Ok(n)
            } else {
                Err(tok)
            }
        })? {
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

    use crate::{blk_arena::BlkArena, block::BlockKind, target::Target, val::Location};

    use super::*;

    type Parser<'scope, 'cb> = super::Parser<'scope, &'scope [u8], &'scope mut dyn FnMut(String)>;

    fn parse_str<F>(s: &[u8], f: F)
    where
        F: FnOnce(Parser, &mut dyn FnMut(&mut Parser)),
    {
        let target = Target::dummy();
        let ty_arena = Arena::new();
        let blk_arena = BlkArena::new();

        let error = Cell::new(false);
        let parser = Parser {
            tok: Tokeniser::new(s, "fname"),
            unit: Unit::new(&target, &ty_arena, &blk_arena),
            sema_error: (&mut |_| error.set(true)) as _,
            names2vals: Default::default(),
        };

        let mut done = false;
        f(parser, &mut |parser| {
            assert!(!error.get(), "sema error during parse");

            parser.eat(Token::Eof).unwrap(); // needed to bump us onto eof()
            assert!(parser.eof());

            done = true;
        });
        assert!(done);
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

            let i4 = parser.unit.types.primitive(Primitive::I4);
            assert_eq!(&v.name, "var1");
            assert_eq!(v.ty, parser.unit.types.ptr_to(i4));
            assert_eq!(v.init.flags,InitFlags::CONSTANT | InitFlags::WEAK);
            assert!(matches!(v.init.init, Init::Int(5)));
        });
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

static void sema_error_if_no_global_ident(parse *p, const char *ident, type **const tout)
{
    global *glob = unit_global_find(p->unit, ident);

    *tout = NULL;

    if(glob){
        *tout = global_type_as_ptr(unit_uniqtypes(p->unit), glob);
    }else{
        sema_error(p, "no such (global) identifier \"%s\"", ident);
        *tout = default_type(p);
    }
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

static void parse_init_ptr(parse *p, type *ty, struct init *init)
{
    /* $ident [+/- value]
     * integer literal */
    init->type = init_ptr;

    switch(token_peek(p->tok)){
        case tok_ident:
        {
            char *ident;
            enum op op;
            long offset = 0;
            type *ident_ty;
            int anyptr = 0;

            eat(p, "pointer initialiser", tok_ident);
            ident = token_last_ident(p->tok);

            sema_error_if_no_global_ident(p, ident, &ident_ty);

            if(token_is_op(token_peek(p->tok), &op)){
                switch(op){
                    case op_add: offset =  1; break;
                    case op_sub: offset = -1; break;
                    default:
                        parse_error(
                                p,
                                "invalid pointer initialiser extra: %s",
                                op_to_str(op));
                }

                if(offset){
                    /* accept add/sub: */
                    token_next(p->tok);

                    eat(p, "int offset", tok_int);
                    offset *= token_last_int(p->tok);
                }
            }

            if(token_accept(p->tok, tok_bareword)){
                char *bareword = token_last_bareword(p->tok);

                if(!strcmp(bareword, "anyptr"))
                    anyptr = 1;
                else
                    parse_error(p, "unexpected \"%s\"", bareword);

                free(bareword);
            }

            if(!anyptr && ty != ident_ty){
                char buf[128];

                sema_error(p,
                        "initialisation type mismatch: init %s with %s",
                        type_to_str_r(buf, sizeof(buf), ty),
                        type_to_str(ident_ty));
            }

            init->u.ptr.is_label = true;
            init->u.ptr.u.ident.label.ident = ident;
            init->u.ptr.u.ident.label.offset = offset;
            init->u.ptr.u.ident.is_anyptr = anyptr;
            break;
        }

        case tok_int:
            init->u.ptr.is_label = false;
            init->u.ptr.u.integral = token_last_int(p->tok);
            token_next(p->tok);
            break;

        default:
            parse_error(p, "pointer initialiser expected");
            memset(&init->u.ptr, 0, sizeof init->u.ptr);
            return;
    }
}
*/
