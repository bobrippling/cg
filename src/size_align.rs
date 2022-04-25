use std::{num::NonZeroU32, ops::Mul};

pub type Size = usize;
pub type Align = NonZeroU32;

#[derive(Debug, PartialEq, Eq, Hash, Clone, Copy)]
pub struct SizeAlign {
    pub size: Size,
    pub align: Align,
}

impl Default for SizeAlign {
    fn default() -> Self {
        Self {
            size: 0,
            align: NonZeroU32::new(1).unwrap(),
        }
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

pub fn gap_for_alignment(current: Size, align: Align) -> Size {
    if current == 0 {
        return 0;
    }

    let align: u32 = align.get();
    if current < align as _ {
        return align as Size - current;
    }

    let bitsover = current % align as usize;
    if bitsover == 0 {
        return 0;
    }

    align as usize - bitsover
}

impl SizeAlign {
    pub fn from_size(sz: NonZeroU32) -> SizeAlign {
        Self {
            size: sz.get() as usize,
            align: sz,
        }
    }
}
