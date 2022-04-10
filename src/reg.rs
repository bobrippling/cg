#[allow(dead_code)]
pub struct Reg {
    idx: u32,
    class: Class,
}

#[allow(dead_code)]
pub enum Class {
    Int,
    Float,
}

#[allow(dead_code)]
impl Reg {
    pub const fn new(idx: u32, class: Class) -> Self {
        Self { idx, class }
    }
}
