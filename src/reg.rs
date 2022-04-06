pub struct Reg {
    idx: u32,
    class: Class,
}

pub enum Class {
    Int,
    Float,
}

impl Reg {
    pub const fn new(idx: u32, class: Class) -> Self {
        Self { idx, class }
    }
}
