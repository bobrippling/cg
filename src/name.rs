use crate::target::Target;

#[derive(Debug, PartialEq, Eq)]
pub struct Name {
    name: String,
    mangled: Option<String>,
}

impl Name {
    pub fn orig(&self) -> &str {
        &self.name
    }

    pub fn mangled(&mut self, _target: &Target) -> &str {
        todo!()
    }

    pub fn new(name: String) -> Name {
        Self { name, mangled: None }
    }
}

impl From<String> for Name {
    fn from(name: String) -> Self {
        Self::new(name)
    }
}
