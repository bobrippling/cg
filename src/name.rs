use once_cell::unsync::OnceCell;

use crate::target::Target;

#[derive(Debug, PartialEq, Eq)]
pub struct Name {
    name: String,
    mangled: OnceCell<String>,
}

impl Name {
    pub fn orig(&self) -> &str {
        &self.name
    }

    pub fn mangled(&self, target: &Target) -> &str {
        if target.sys.leading_underscore {
            self.mangled.get_or_init(|| format!("_{}", self.name))
        } else {
            self.orig()
        }
    }

    pub fn new(name: String) -> Name {
        Self {
            name,
            mangled: OnceCell::new(),
        }
    }
}

impl From<String> for Name {
    fn from(name: String) -> Self {
        Self::new(name)
    }
}
