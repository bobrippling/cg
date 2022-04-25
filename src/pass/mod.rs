use crate::unit::Unit;

pub mod abi;
pub mod isel;
pub mod expand_builtins;
pub mod regalloc;

pub trait Pass {
    fn run(&mut self, unit: &mut Unit);
}
