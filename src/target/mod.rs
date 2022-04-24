#![allow(dead_code)]

use std::str::FromStr;

use crate::{regset::RegSet, size_align::{SizeAlign, Align}};
mod x86;

#[derive(Debug)]
pub struct Target {
    pub sys: Sys,
    pub arch: Arch,
    pub abi: Abi,
}

#[derive(Debug)]
pub struct Arch {
    pub ptr: SizeAlign,
    // instructions: ...
    op_isn_is_destructive: bool,
}

#[derive(Debug)]
pub struct Sys {
    lbl_priv_prefix: &'static str,
    section_rodata: &'static str,
    weak_directive_var: &'static str,
    weak_directive_func: &'static str,
    align_is_pow2: bool,
    leading_underscore: bool,
    pub pic: Pic,
}

#[derive(Debug)]
pub struct Pic {
    pub active: bool,
    pub call_requires_plt: bool,
}

#[derive(Debug, Clone)]
pub struct Abi {
    scratch_regs: RegSet,
    ret_regs: RegSet,
    arg_regs: RegSet,
    callee_saves: RegSet,
}

impl FromStr for Target {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let mut sys = None;
        let mut arch_abi = None;

        for part in s.split('-') {
            if let Ok(s) = part.parse() {
                sys = Some(s);
                continue;
            }
            if let Some(a) = Arch::parse(part) {
                arch_abi = Some(a);
                continue;
            }
            return Err(format!("unrecognised target part '{}'", part));
        }

        match (sys, arch_abi) {
            (Some(sys), Some((arch,abi))) => Ok(Target { arch, abi, sys }),
            (None, _) => Err("missing sysname".into()),
            (_, None) => Err("missing arch/abi".into()),
        }
    }
}

impl Target {
    pub fn default() -> Result<Self, Box<dyn std::error::Error>> {
        let mut uname = uname::uname()?;

        if uname.machine == "amd64" {
            uname.machine = "x86_64".into();
        }
        uname.sysname = uname.sysname.to_lowercase();

        let sys = uname.sysname.parse().map_err(|_| "couldn't parse system name")?;
        let (arch, abi) = Arch::parse(&uname.machine).ok_or("couldn't parse machine name")?;

        Ok(Target { arch, abi, sys })
    }
}

impl Arch {
    fn parse(s: &str) -> Option<(Arch, Abi)> {
        match s {
            "x86_64" => Some((
                Arch {
                    ptr: SizeAlign { size: 8, align: Align::new(8).unwrap() },
                    op_isn_is_destructive: true,
                },
                x86::ABI.clone(),
            )),
            _ => None,
        }
    }
}

impl FromStr for Sys {
    type Err = ();

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "linux" => Ok(Sys {
                lbl_priv_prefix: ".L",
                section_rodata: ".rodata",
                weak_directive_var: ".weak",
                weak_directive_func: ".weak",
                align_is_pow2: false,      /* align_is_pow2 */
                leading_underscore: false, /* leading_underscore */
                pic: Pic {
                    active: false,
                    call_requires_plt: true,
                },
            }),
            "darwin" => Ok(Sys {
                lbl_priv_prefix: "L",
                section_rodata: ".section __TEXT,__const",
                weak_directive_var: ".weak_reference",
                weak_directive_func: ".weak_definition",
                align_is_pow2: true,
                leading_underscore: true,
                pic: Pic {
                    active: true,
                    call_requires_plt: false,
                },
            }),
            _ => Err(()),
        }
    }
}

#[cfg(test)]
impl Target {
    pub fn dummy() -> Self {
        let (arch, abi) = Arch::parse("x86_64").unwrap();
        Self {
            sys: Sys::from_str("linux").unwrap(),
            arch,
            abi,
        }
    }
}
