#[macro_export]
macro_rules! enum_string {
    (
        pub enum $name:ident {
            $($member:ident = $str:expr),*
            $(,)*
        }
    ) => {
        #[derive(PartialEq, Eq, Debug, Clone, Copy)]
        pub enum $name {
            $($member),*
        }

        impl TryFrom<&str> for $name {
            type Error = ();

            fn try_from(value: &str) -> Result<Self, Self::Error> {
                $(
                    if value.starts_with($str) {
                        return Ok(Self::$member);
                    }
                )*
                Err(())
            }
        }

        impl $name {
            fn str(self) -> &'static str {
                match self {
                    $($name::$member => $str),*
                }
            }

            pub fn len(&self) -> usize {
                self.str().len()
            }
        }

    }
}
