use std::{cell::RefCell, collections::HashMap};

use crate::{
    size_align::SizeAlign,
    ty::{Primitive, Type, TypeS},
};

pub struct TyUniq<'t>(RefCell<Inner<'t>>);

struct Inner<'t> {
    primitives: HashMap<Primitive, TypeS<'t>>,
    void: Option<Box<TypeS<'t>>>,

    structs: Vec<TypeS<'t>>,
    aliases: HashMap<String, TypeS<'t>>,

    ptr: SizeAlign,
}

impl<'t> TyUniq<'t> {
    pub fn new(ptr: SizeAlign) -> Self {
        Self(RefCell::new(Inner {
            primitives: HashMap::new(),
            void: None,

            structs: vec![],
            aliases: HashMap::new(),

            ptr,
        }))
    }

    pub fn primitive(&mut self, p: Primitive) -> Type<'t> {
	todo!()
	/*
        let t = self
            .0
            .borrow_mut()
            .primitives
            .entry(p)
            .or_insert_with(|| TypeS::Primitive(p));

        t
	*/
    }

    pub fn void(&mut self) -> Type<'t> {
	todo!()
    }

    pub fn struct_of(&mut self, types: Vec<Type<'t>>) -> Type<'t> {
	todo!()
    }

    pub fn resolve_alias(&mut self, spel: &str) -> Option<Type<'t>> {
	todo!()
    }

    pub fn default(&mut self) -> Type<'t> {
	todo!()
    }

    pub fn array_of(&mut self, elemty: Type<'t>, n: usize) -> Type<'t> {
        todo!()
    }

    pub fn ptr_to(&self, pointee: Type<'t>) -> Type<'t> {
        todo!()
    }

    pub fn func_of(&self, ret: Type<'t>, types: Vec<Type<'t>>, variadic: bool) -> Type<'t> {
        todo!()
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn memory() {
        let mut ut = TyUniq::new(SizeAlign { size: 8, align: 8 });

        let i4_a = ut.primitive(Primitive::I4);
        let i4_b = ut.primitive(Primitive::I4);

	assert_eq!(i4_a, i4_b);
    }
}
