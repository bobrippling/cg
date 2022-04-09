#[derive(Clone, Copy)]
pub struct SrcLoc {
    pub line: u32,
    pub col: u32,
}

impl SrcLoc {
    pub fn start() -> Self {
        Self { line: 1, col: 1 }
    }
}

impl std::fmt::Debug for SrcLoc {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}:{}", self.line, self.col)
    }
}
