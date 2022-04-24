use std::ops::Mul;

pub type Size = usize;
pub type Align = u32;

#[derive(Debug, PartialEq, Eq, Hash, Clone, Copy)]
pub struct SizeAlign {
    pub size: Size,
    pub align: Align,
}

impl Default for SizeAlign {
    fn default() -> Self {
        Self { size: 0, align: 1 }
    }
}

impl Mul<Size> for SizeAlign {
    type Output = Self;

    fn mul(self, rhs: Size) -> Self::Output {
        Self {
            size: self.size * rhs,
            ..self
        }
    }
}

pub fn gap_for_alignment(current: Align, align: Align) -> Size {
    if current == 0 {
        return 0;
    }

    if current < align {
        return (align - current) as _;
    }

    let bitsover = current % align;
    if bitsover == 0 {
        return 0;
    }

    (align - bitsover) as _
}

impl SizeAlign {
    pub fn from_size(sz: usize) -> SizeAlign {
        Self {
            size: sz,
            align: sz.try_into().unwrap(),
        }
    }
}
