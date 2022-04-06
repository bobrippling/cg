pub mod abi;
pub mod isel;
pub mod expand_builtins;
pub mod regalloc;

pub trait Pass {
    fn run(&self) {
        todo!()
    }
}
