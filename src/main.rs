mod blk_arena;
mod enum_string;

mod parse;
mod srcloc;
mod token;
mod tokenise;

mod size_align;
mod target;
mod unit;

mod reg;
mod regset;

mod func;
mod name;
mod global;
mod ty;
mod ty_uniq;
mod val;
mod location;
mod variable;

mod init;

mod block;
mod isn;
mod lbl;
mod dag;

mod pass;

//mod opt_cprop;
//mod opt_dse;
//mod opt_loadmerge;
//mod opt_storeprop;

use std::{
    error::Error,
    fs::File,
    io::{BufRead, BufReader, Write},
    process,
};

use clap::Parser;
use typed_arena::Arena;

use blk_arena::BlkArena;
// use func::Func;
use target::Target;
use tokenise::Tokeniser;
use ty::TypeS;
use unit::Unit;

type Result<T> = std::result::Result<T, Box<dyn Error>>;

fn read_and_parse<'scope>(
    fname: Option<&'scope str>,
    dump_tok: bool,
    target: &'scope Target,
    ty_arena: &'scope Arena<TypeS<'scope>>,
    blk_arena: &'scope BlkArena<'scope>,
) -> Result<Option<Unit<'scope>>> {
    let (file, stdin);
    let (reader, fname): (Box<dyn BufRead>, _) = match fname {
        Some(fname) => {
            file = File::open(fname)?;
            (Box::new(BufReader::new(file)), fname)
        }
        None => {
            stdin = std::io::stdin();
            (Box::new(BufReader::new(stdin)), "-")
        }
    };

    let tok = Tokeniser::new(reader, fname);

    if dump_tok {
        for t in tok.into_iter() {
            println!("{:?}", t);
        }
        Ok(None)
    } else {
        let mut sema_err = Ok(());
        let unit = Unit::parse(tok, target, ty_arena, blk_arena, |e| {
            sema_err = Err(e);
        })
        .map_err(|(parse_err, location)| {
            // parse error
            format!("{:?}: {}", location, parse_err)
        })?;

        sema_err?;

        Ok(Some(unit))
    }
}

/// Intermediate representation code generator
#[derive(Parser, Debug)]
#[clap(version)]
struct Opts {
    /// Optimisation pass(es) to run
    #[clap(short = 'O')]
    opt: Vec<String>,

    /// Output filename
    #[clap(short = 'o')]
    output: Option<String>,

    /// Dump parsed tokens
    #[clap(long)]
    dump_tokens: bool,

    /// Show intermediate IR after each pass
    #[clap(long)]
    show_intermediates: bool, // check kebab-case

    /// Position independent code
    #[clap(long, parse(try_from_str = parse_pic))]
    pic: Option<bool>,

    /// Emit for a given target
    #[clap(long = "emit")]
    target: Option<Target>,

    /// Input file
    fname: Option<String>,
}

fn parse_pic(s: &str) -> std::result::Result<bool, &'static str> {
    match s {
        "true" => Ok(true),
        "false" => Ok(false),
        _ => Err("expected `true` or `false`"),
    }
}

fn main() -> Result<()> {
    // FILE *fout;
    // bool dump_tok = false;
    // unit *unit = NULL;
    // const char *fname = NULL;
    // const char *output = NULL;
    // int i;
    // int parse_err = 0;
    // struct target target = { 0 };
    // const char *emit_arg = NULL;
    // struct passes_and_target pat = { 0 };
    // struct parsed_options opts = { TRISTATE_UNSET };

    let mut opts = Opts::parse();

    match opts.fname {
        Some(s) if s == "-" => {
            opts.fname = None;
        }
        _ => {}
    }

    let mut target = match opts.target {
        Some(t) => t,
        None => Target::default()?,
    };
    if let Some(pic) = opts.pic {
        target.sys.pic.active = pic;
    }

    let fname: Option<&str> = match opts.fname {
        Some(ref s) => Some(&s),
        None => None,
    };

    let ty_arena = Arena::new();
    let blk_arena = BlkArena::new();
    let mut unit = match read_and_parse(fname, opts.dump_tokens, &target, &ty_arena, &blk_arena)? {
        Some(u) => u,
        None => process::exit(0),
    };

    unit.run_pass(&mut pass::expand_builtins::Pass);

    unit.run_pass(&mut pass::to_dag::Pass);

    /* ensure the final passes are: */
    unit.run_pass(&mut pass::abi::Pass::new());
    unit.run_pass(&mut pass::isel::Pass);
    // unit.run_pass(&mut pass::spill::Pass);
    unit.run_pass(&mut pass::regalloc::Pass);

    // TODO
    // if(pat->show_intermediates)
    // 	printf("------- %s -------\n", passes[j].spel);
    // 	function_dump(fn, stdout);

    let (mut file, mut stdout);
    let fout: &mut dyn Write = match opts.output {
        Some(output) => {
            file = File::create(output)?;
            &mut file
        }
        None => {
            stdout = std::io::stdout();
            &mut stdout
        }
    };

    for g in unit.globals.iter_mut() {
        unit.target.emit(g, &unit.target, &unit.types, fout)?;
    }

    Ok(())
}
