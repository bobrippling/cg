#![allow(dead_code)]

#[derive(Debug, Clone, Copy)]
pub struct Reg {
    idx: u32,
    class: Class,
}

#[derive(Debug, Clone, Copy)]
pub enum Class {
    Int,
    Float,
}

impl Reg {
    pub const fn new(idx: u32, class: Class) -> Self {
        Self { idx, class }
    }

    pub fn is_int(&self) -> bool {
        match self.class {
            Class::Int => true,
            Class::Float => false,
        }
    }

    pub fn is_fp(&self) -> bool {
        match self.class {
            Class::Int => false,
            Class::Float => true,
        }
    }
}
