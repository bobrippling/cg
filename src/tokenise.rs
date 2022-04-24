use std::io::{BufRead, BufReader, Read};

use thiserror::Error;

use crate::srcloc::SrcLoc;
use crate::token::{Keyword, Punctuation, Token};

type IoResult<T> = std::io::Result<T>;
pub type LexResult = std::result::Result<Token, Error>;

#[derive(Error, Debug)]
pub enum Error {
    #[error(transparent)]
    IoError(#[from] std::io::Error),
    #[error("unknown token")]
    UnknownToken(String),
    #[error("no terminating quote to string")]
    UnterminatedStr(String),
}

pub struct Tokeniser<'a, R> {
    _fname: &'a str,
    line_no: u32,

    reader: BufReader<R>,
    line: String,
    eof: bool,
    offset: usize,

    unget: Option<Token>,
}

impl<'a, R> Tokeniser<'a, R>
where
    R: Read,
{
    pub fn new(reader: R, fname: &'a str) -> Self {
        let reader = BufReader::new(reader);

        Self {
            _fname: fname,
            line_no: 1,

            reader,
            line: String::new(),
            eof: false,
            offset: 0,

            unget: None,
        }
    }

    pub fn loc(&self) -> SrcLoc {
        SrcLoc {
            line: self.line_no,
            col: self.offset as u32,
        }
    }

    pub fn eof(&self) -> bool {
        self.eof
    }

    pub fn unget(&mut self, tok: Token) {
        let old = self.unget.replace(tok);
        assert!(old.is_none());
    }

    fn next_line(&mut self) -> IoResult<()> {
        self.offset = 0;
        self.line.truncate(0);
        let n = self.reader.read_line(&mut self.line)?;

        if n == 0 {
            self.eof = true;
        }
        self.line_no += 1;

        Ok(())
    }

    fn skip_space(&mut self) -> IoResult<()> {
        self.next_while(|b| b.is_ascii_whitespace())
    }

    fn next_while<P>(&mut self, mut pred: P) -> IoResult<()>
    where
        P: FnMut(u8) -> bool,
    {
        loop {
            for ch in self.line.bytes().skip(self.offset) {
                if pred(ch) {
                    self.offset += 1;
                    if self.offset == self.line.len() {
                        break;
                    }
                } else {
                    return Ok(());
                }
            }

            self.next_line()?;
            if self.eof {
                return Ok(());
            }
        }
    }

    pub fn next(&mut self) -> LexResult {
        if let Some(unget) = self.unget.take() {
            return Ok(unget);
        }

        if self.eof {
            return Ok(Token::Eof);
        }

        self.skip_space()?;

        let line = &self.line[self.offset..];

        let ch = match line.bytes().next() {
            Some(ch) => ch,
            None => return Ok(Token::Eof),
        };

        if ch == b'#' {
            self.next_line()?;
            return self.next();
        }

        if let Ok(p) = Punctuation::try_from(line) {
            self.offset += p.len();
            return Ok(Token::Punctuation(p));
        }

        if ch.is_ascii_digit() || ch == b'-' {
            let neg = ch == b'-';
            let (n, count) = line
                .bytes()
                .skip(if neg { 1 } else { 0 })
                .take_while(u8::is_ascii_digit)
                .fold((0i32, 0), |(num, count), byte| {
                    (num * 10 + (byte - b'0') as i32, count + 1)
                });

            self.offset += count;

            return Ok(Token::Integer(if neg { -n } else { n }));
        }

        if let Ok(kw) = Keyword::try_from(line) {
            self.offset += kw.len();
            return Ok(Token::Keyword(kw));
        }

        if ch == b'$' {
            self.offset += 1;
            if let Some(ident) = self.parse_ident() {
                return Ok(Token::Identifier(ident.into()));
            }
            self.offset -= 1;
        }

        if ch.is_ident(false) {
            if let Some(bare) = self.parse_ident() {
                return Ok(Token::Bareword(bare.into()));
            }
        }

        if ch == b'"' {
            let (s, bytelen) = self
                .parse_string()
                .ok_or(Error::UnterminatedStr(self.line_remaining().into()))?;

            self.offset += bytelen;

            return Ok(Token::String(s));
        }

        return Err(Error::UnknownToken(self.line_remaining().into()));
    }

    fn line_remaining(&self) -> &str {
        &self.line[self.offset..]
    }

    fn parse_ident(&mut self) -> Option<&str> {
        self.line
            .bytes()
            .skip(self.offset)
            .enumerate()
            .take_while(|&(i, b)| b.is_ident(i > 0))
            .last()
            .map(|(i, _)| {
                let slice = &self.line[self.offset..][..i + 1];
                self.offset += i + 1;
                slice
            })
    }

    fn parse_string(&mut self) -> Option<(Box<[u8]>, usize)> {
        todo!()
    }
}

impl<'a, R> Tokeniser<'a, R>
where
    R: Read + 'a,
{
    pub fn into_iter(mut self) -> impl Iterator<Item = Result<Token, Error>> + 'a {
        std::iter::from_fn(move || match self.next() {
            Ok(Token::Eof) => None,
            o @ Ok(_) => Some(o),
            e @ Err(_) => Some(e),
        })
    }
}

trait IsIdent {
    fn is_ident(self, inc_digit: bool) -> bool
    where
        Self: Sized;
}

impl IsIdent for u8 {
    fn is_ident(self, inc_digit: bool) -> bool
    where
        Self: Sized,
    {
        self.is_ascii_alphabetic() || self == b'_' || (inc_digit && self.is_ascii_digit())
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn tokenisation() {
        let s: &[u8] = b"
        $main = i4() { ret i4 0 }
        $i = i8* external $l add 1
        ...
        .
        ,
        ";

        let tok = Tokeniser::new(s, "");

        let tokens = tok.into_iter().collect::<Result<Vec<_>, _>>().unwrap();

        assert_eq!(
            tokens,
            vec![
                Token::Identifier("main".into()),
                Token::Punctuation(Punctuation::Equal),
                Token::Keyword(Keyword::I4),
                Token::Punctuation(Punctuation::LParen),
                Token::Punctuation(Punctuation::RParen),
                Token::Punctuation(Punctuation::LBrace),
                Token::Keyword(Keyword::Ret),
                Token::Keyword(Keyword::I4),
                Token::Integer(0),
                Token::Punctuation(Punctuation::RBrace),
                //
                Token::Identifier("i".into()),
                Token::Punctuation(Punctuation::Equal),
                Token::Keyword(Keyword::I8),
                Token::Punctuation(Punctuation::Star),
                Token::Bareword("external".into()),
                Token::Identifier("l".into()),
                Token::Op(Op::Add),
                Token::Integer(1),
                Token::Punctuation(Punctuation::Ellipses),
                Token::Punctuation(Punctuation::Dot),
                Token::Punctuation(Punctuation::Comma),
            ]
        );
    }
}

/*
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>

#include "mem.h"
#include "macros.h"
#include "str.h"
#include "io.h"

#include "tokenise.h"

#define TOKEN_PEEK_EMPTY tok_eof

struct tokeniser
{
    FILE *f;
    char *str;

    int eof;
    int ferr;
    unsigned lno;

    const char *fname;
    char *line, *linep, *last_tok_start;
    enum token unget;

    char *lastident;
    char *lastbareword;
    char *laststring;
    size_t laststringlen;
    long lastint;
};

static const struct
{
    const char *kw;
    enum token tok;
} keywords[] = {
#define KW(t) { #t, tok_ ## t },
#define OTHER(t)
#define PUNCT(t, s)
#define PUNCTSTR(t, s)
#define OP KW
#define CMP KW
    TOKENS
#undef CMP
#undef OP
#undef PUNCTSTR
#undef PUNCT
#undef OTHER
#undef KW
};

tokeniser *token_init(FILE *f, const char *fname)
{
    tokeniser *t = xmalloc(sizeof *t);
    memset(t, 0, sizeof *t);

    t->f = f;
    t->fname = fname;
    t->unget = TOKEN_PEEK_EMPTY;

    return t;
}

tokeniser *token_init_str(const char *str)
{
    tokeniser *t = xmalloc(sizeof *t);
    memset(t, 0, sizeof *t);

    t->unget = TOKEN_PEEK_EMPTY;
    t->str = xstrdup(str);

    return t;
}

static char *token_read_line(tokeniser *t)
{
    char *r;

    if(t->f){
        char *l = read_line(t->f);
        if(errno){
            fprintf(stderr, "read: %s\n", strerror(errno));
            free(l);
            l = NULL;
        }
        return l;
    }

    r = t->str;
    t->str = NULL;
    return r;
}

static void token_free_line(tokeniser *t)
{
    assert(!t->line || !t->str);
    free(t->line);
    t->line = NULL;
}

void token_fin(tokeniser *t, int *const err)
{
    if(t->f){
        int failed = fclose(t->f);

        if(!t->ferr && failed)
            t->ferr = errno;
    }

    *err = t->ferr;

    free(t->lastident);
    free(t->lastbareword);
    free(t->str);
    free(t);
}

const char *token_to_str(enum token t)
{
    switch(t){
#define OTHER(x) case tok_ ## x: return #x;
#define KW(x) case tok_ ## x: return "tok_" #x;
#define PUNCT(x, p) case tok_ ## x: return #p;
#define PUNCTSTR(x, p) case tok_ ## x: return p;
#define OP OTHER
#define CMP OTHER
        TOKENS
#undef OTHER
#undef KW
#undef PUNCTSTR
#undef PUNCT
#undef OP
#undef CMP
    }
    assert(0);
}

static int consume_word(tokeniser *t, const char *word)
{
    if(!str_beginswith(t->linep, word))
        return 0;

    if(isident(t->linep[strlen(word)], 1))
        return 0;

    t->linep += strlen(word);
    return 1;
}

static char *tokenise_ident(tokeniser *t)
{
    char *end;
    char *buf;
    size_t len;

    for(end = t->linep + 1; isident(*end, 1); end++);

    len = end - t->linep + 1;
    buf = xmalloc(len);
    memcpy(buf, t->linep, len - 1);
    buf[len - 1] = '\0';

    t->linep += len - 1;

    return buf;
}

static bool octdigit(int ch)
{
    return '0' <= ch && ch < '8';
}

static enum token parse_string(tokeniser *t)
{
    char *const start = t->linep;
    char *str;
    char *end, *p;
    size_t stri, len;

    ++t->linep;

    /* string is alphanumeric or /\\[0-7]{1,3}/ */
    end = strchr(t->linep, '"');
    if(!end){
        fprintf(stderr, "no terminating '\"' in '%s'\n", start);
        return tok_unknown;
    }

    len = end - t->linep;
    str = xmalloc(len);
    t->linep = end + 1;

    for(p = start + 1, stri = 0; p != end; p++, stri++){
        if(*p == '\\'){
            const size_t n_to_end = end - (p + 1);
            char buf[4];
            int octval;
            size_t i;

            if(n_to_end == 0){
                fprintf(stderr, "empty escape sequence in '%s'\n", start);
                goto bad_esc;
            }

            for(i = 0; i < MIN(n_to_end, 3); i++){
                buf[i] = p[i + 1];

                if(!octdigit(buf[i])){
                    buf[i] = '\0';
                    break;
                }
            }
            buf[i] = '\0';

            if(sscanf(buf, "%o", &octval) != 1){
                fprintf(stderr, "bad escape sequence in '%s'\n", start);
                goto bad_esc;
            }

            str[stri] = octval;
            p += strlen(buf);
        }else{
            str[stri] = *p;
        }
    }

    free(t->laststring);
    t->laststring = str;
    t->laststringlen = stri;
    assert(stri <= len);

    return tok_string;
bad_esc:
    free(str);
    return tok_unknown;
}

enum token token_next(tokeniser *t)
{
    size_t i;

    t->last_tok_start = NULL;

    if(t->unget != TOKEN_PEEK_EMPTY){
        enum token unget = t->unget;
        t->unget = TOKEN_PEEK_EMPTY;
        return unget;
    }

    if(t->eof)
        return tok_eof;

    t->linep = skipspace(t->linep);

    while(!t->linep || !*t->linep){
        token_free_line(t);
        t->line = t->linep = token_read_line(t);

        if(!t->line){
            t->eof = 1;

            if(t->f && ferror(t->f))
                t->ferr = errno;

            /* file closed later */
            return tok_eof;
        }

        t->lno++;
        t->linep = skipspace(t->linep);
    }

    t->last_tok_start = t->linep;

    /* check for long string before single chars - handle '...' before '.' */
#define OTHER(x)
#define KW(x)
#define PUNCT(t, c)
#define PUNCTSTR(tok, s) \
    if(!strncmp(t->linep, s, strlen(s))){ \
        t->linep += strlen(s); \
        return tok_ ## tok; \
    }
#define OP(x)
#define CMP(x)
    TOKENS
#undef OTHER
#undef KW
#undef PUNCTSTR
#undef PUNCT
#undef OP
#undef CMP

    switch(*t->linep++){
#define OTHER(x)
#define KW(x)
#define PUNCT(t, c) case c: return tok_ ## t;
#define PUNCTSTR(t, c)
#define OP(x)
#define CMP(x)
        TOKENS
#undef OTHER
#undef KW
#undef PUNCTSTR
#undef PUNCT
#undef OP
#undef CMP

        case '#':
            for(; *t->linep && *t->linep != '\n'; t->linep++);
            return token_next(t);

        default:
            t->linep--;
    }

    if(isdigit(*t->linep) || (*t->linep == '-' && isdigit(t->linep[1]))){
        char *end;
        t->lastint = strtol(t->linep, &end, 0);
        assert(end > t->linep);
        t->linep = end;
        return tok_int;
    }

    for(i = 0; i < countof(keywords); i++)
        if(consume_word(t, keywords[i].kw))
            return keywords[i].tok;

    if(*t->linep == '$' && isident(t->linep[1], 1)){
        ++t->linep;

        free(t->lastident);
        t->lastident = tokenise_ident(t);

        return tok_ident;
    }

    if(isident(*t->linep, 0)){
        free(t->lastbareword);
        t->lastbareword = tokenise_ident(t);
        return tok_bareword;
    }

    if(*t->linep == '"'){
        return parse_string(t);
    }

    fprintf(stderr, "unknown token '%s'\n", t->linep);

    return tok_unknown;
}

enum token token_peek(tokeniser *t)
{
    if(t->unget == TOKEN_PEEK_EMPTY)
        t->unget = token_next(t);

    return t->unget;
}

bool token_accept(tokeniser *t, enum token tk)
{
    if(token_peek(t) == tk){
        token_next(t);
        return true;
    }
    return false;
}

void token_curline(tokeniser *t, char *out, size_t len, size_t *const poff)
{
    const char *src = t->line;
    const char *i, *anchor = NULL;
    size_t off;

    *poff = 0;

    if(!src){
        xsnprintf(out, len, "[eof]");
        return;
    }

    off = t->linep - src;

    if(off > len / 2)
        src = t->linep - len / 2;

    /* ensure we're not before linep's line */
	for(i = src; i < t->linep; i++)
		if(*i == '\n')
			anchor = i + 1;

	if(anchor)
		src = anchor;

	for(; len > 1 && *src && *src != '\n'; src++, out++, len--)
		*out = *src;
	*out = '\0';

	if(t->last_tok_start)
		*poff = (t->last_tok_start - t->line) - (src - t->line);
}

unsigned token_curlineno(tokeniser *t)
{
	return t->lno;
}

const char *token_curfile(tokeniser *t)
{
	return t->fname;
}

int token_last_int(tokeniser *t)
{
	return t->lastint;
}

static char *token_last_(char **p)
{
	char *ret = *p;
	*p = NULL;

	if(!ret)
		ret = xstrdup("?");

	return ret;
}

char *token_last_ident(tokeniser *t)
{
	return token_last_(&t->lastident);
}

const char *token_last_ident_peek(tokeniser *t)
{
	return t->lastident;
}

char *token_last_bareword(tokeniser *t)
{
	return token_last_(&t->lastbareword);
}

void token_last_string(tokeniser *t, struct string *const str)
{
	str->len = t->laststringlen;
	str->str = token_last_(&t->laststring);
}

int token_is_op(enum token t, enum op *const o)
{
	switch(t){
#define KW(t) case tok_ ## t: break;
#define OTHER KW
#define PUNCT(t, c) case tok_ ## t: break;
#define PUNCTSTR(t, c) case tok_ ## t: break;
#define OP(t) case tok_ ## t: *o = op_ ## t; return 1;
#define CMP KW
	TOKENS
#undef CMP
#undef OP
#undef PUNCTSTR
#undef PUNCT
#undef OTHER
#undef KW
	}
	return 0;
}

int token_is_cmp(enum token t, enum op_cmp *const o)
{
	switch(t){
#define KW(t) case tok_ ## t: break;
#define OTHER KW
#define PUNCT(t, c) case tok_ ## t: break;
#define PUNCTSTR(t, c) case tok_ ## t: break;
#define OP(t) case tok_ ## t: break;
#define CMP(t) case tok_ ## t: *o = cmp_ ## t; return 1;
	TOKENS
#undef CMP
#undef OP
#undef PUNCTSTR
#undef PUNCT
#undef OTHER
#undef KW
	}
	return 0;
}
*/
