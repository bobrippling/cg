use std::{cell::RefCell, collections::HashMap};

use typed_arena::Arena;

use crate::{
    size_align::SizeAlign,
    ty::{Primitive, Type, TypeS},
};

type FuncKey<'t> = (Type<'t>, Vec<Type<'t>>, bool);

pub struct TyUniq<'t> {
    arena: &'t Arena<TypeS<'t>>,

    primitives: HashMap<Primitive, Type<'t>>,
    void: Option<Type<'t>>,

    structs: HashMap<Vec<Type<'t>>, Type<'t>>,
    arrays: HashMap<(Type<'t>, usize), Type<'t>>,
    pointers: HashMap<Type<'t>, Type<'t>>,
    funcs: HashMap<FuncKey<'t>, Type<'t>>,
    aliases: HashMap<String, Type<'t>>,

    ptr: SizeAlign,
}

impl<'t> TyUniq<'t> {
    pub fn new(ptr: SizeAlign, arena: &'t Arena<TypeS<'t>>) -> Self {
        Self {
            primitives: HashMap::new(),
            void: None,

            structs: HashMap::new(),
            arrays: HashMap::new(),
            pointers: HashMap::new(),
            funcs: HashMap::new(),
            aliases: HashMap::new(),

            ptr,
            arena,
        }
    }

    pub fn primitive<'s>(&'s mut self, p: Primitive) -> Type<'t>
    where
        't: 's,
    {
        if let Some(&t) = self.primitives.get(&p) {
            return t;
        }

        let t = self.arena.alloc(TypeS::Primitive(p));
        self.primitives.insert(p, t);
        t
    }

    pub fn void<'s>(&'s mut self) -> Type<'t>
    where
        't: 's,
    {
        self.void.get_or_insert_with(|| {
            let p = self.arena.alloc(TypeS::Void);
            p
        })
    }

    pub fn struct_of(&mut self, membs: Vec<Type<'t>>) -> Type<'t> {
        self.structs
            .entry(membs)
            .or_insert_with_key(|key| self.arena.alloc(TypeS::Struct { membs: key.clone() }))
    }

    pub fn default(&mut self) -> Type<'t> {
        self.primitive(Primitive::I4)
    }

    pub fn array_of(&mut self, elem: Type<'t>, n: usize) -> Type<'t> {
        self.arrays
            .entry((elem, n))
            .or_insert_with(|| self.arena.alloc(TypeS::Array { elem, n }))
    }

    pub fn ptr_to(&mut self, pointee: Type<'t>) -> Type<'t> {
        self.pointers
            .entry(pointee)
            .or_insert_with(|| self.arena.alloc(TypeS::Ptr { pointee }))
    }

    pub fn func_of(&mut self, ret: Type<'t>, args: Vec<Type<'t>>, variadic: bool) -> Type<'t> {
        self.funcs
            .entry((ret, args, variadic))
            .or_insert_with_key(|key| {
                self.arena.alloc(TypeS::Func {
                    ret,
                    args: key.1.clone(),
                    variadic,
                })
            })
    }

    pub fn resolve_alias(&mut self, spel: &str) -> Option<Type<'t>> {
        self.aliases.get(spel).copied()
    }

    pub fn add_alias(&mut self, spel: &str, actual: Type<'t>) -> Type<'t> {
        self.aliases.entry(spel.into()).or_insert_with_key(|key| {
            self.arena.alloc(TypeS::Alias {
                name: key.into(),
                actual,
            })
        })
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn memory() {
        let arena = Arena::new();
        let mut ut = TyUniq::new(SizeAlign { size: 8, align: 8 }, &arena);

        let i4_a = ut.primitive(Primitive::I4);
        let i4_b = ut.primitive(Primitive::I4);

        assert_eq!(i4_a, i4_b);
    }

    #[test]
    fn aliases() {
        let arena = Arena::new();
        let mut ut = TyUniq::new(SizeAlign { size: 8, align: 8 }, &arena);

        assert!(ut.resolve_alias("size_t").is_none());
        assert_eq!(ut.arena.len(), 0);

        let usz = ut.primitive(Primitive::I8);
        assert_eq!(ut.arena.len(), 1);

        ut.add_alias("size_t", usz);
        assert_eq!(ut.arena.len(), 2);

        assert_eq!(ut.resolve_alias("size_t"), Some(usz));

        ut.add_alias("size_t", usz);
        assert_eq!(ut.arena.len(), 2);
        assert_eq!(ut.resolve_alias("size_t"), Some(usz));
    }

    #[test]
    fn structs() {
        let arena = Arena::new();
        let mut ut = TyUniq::new(SizeAlign { size: 8, align: 8 }, &arena);

        let usz = ut.primitive(Primitive::I8);
        let int = ut.primitive(Primitive::I4);
        let ch = ut.primitive(Primitive::I1);

        assert_eq!(ut.arena.len(), 3);

        let st1 = ut.struct_of(vec![usz, int]);
        assert_eq!(ut.arena.len(), 4);
        let st2 = ut.struct_of(vec![int, ch]);
        assert_eq!(ut.arena.len(), 5);
        assert!(st1 != st2);

        let st3 = ut.struct_of(vec![usz, int]);
        assert_eq!(ut.arena.len(), 5);
        assert_eq!(st1, st3);
    }
}
