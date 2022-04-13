use typed_arena::Arena;

use crate::{block::Block, isn::Isn};

pub struct BlkArena<'arena> {
    pub blks: Arena<Block<'arena>>,
    pub isns: Arena<Isn<'arena>>,
}

impl<'arena> BlkArena<'arena> {
    pub fn new() -> BlkArena<'arena> {
        Self {
            blks: Arena::new(),
            isns: Arena::new(),
        }
    }
}
