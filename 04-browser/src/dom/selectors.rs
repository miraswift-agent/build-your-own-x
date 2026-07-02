//! CSS selector parsing and matching.
//!
//! Supports:
//! - Type selectors: `div`, `p`, `*`
//! - Class selectors: `.foo`
//! - ID selectors: `#bar`
//! - Attribute selectors: `[attr]`, `[attr=val]`, `[attr~=val]`, `[attr|=val]`,
//!   `[attr^=val]`, `[attr$=val]`, `[attr*=val]`
//! - Combinators: descendant (space), child `>`, adjacent sibling `+`, general sibling `~`
//! - Pseudo-classes: `:first-child`, `:last-child`, `:only-child`, `:nth-child(n)`,
//!   `:nth-last-child(n)`, `:not(...)`, `:empty`, `:root`
//! - Selector lists: `,`

use std::collections::VecDeque;
use crate::html::dom::{Document, NodeData, NodeId, DOCUMENT_NODE_ID};

// ─── Selector AST ─────────────────────────────────────────────────────────────

/// Attribute matching operator.
#[derive(Debug, Clone, PartialEq)]
pub enum AttrOp {
    Exact,      // [attr=val]
    Includes,   // [attr~=val]  — space-separated word match
    DashMatch,  // [attr|=val]  — val or val-…
    Prefix,     // [attr^=val]
    Suffix,     // [attr$=val]
    Substring,  // [attr*=val]
}

/// An attribute selector with optional operator/value.
#[derive(Debug, Clone)]
pub struct AttrSelector {
    pub name: String,
    pub op_value: Option<(AttrOp, String)>,
}

/// Argument to `:nth-child`.
#[derive(Debug, Clone)]
pub enum NthArg {
    Even,
    Odd,
    /// A fixed index (1-based).
    Index(u32),
    /// `an+b` formula.
    AnPlusB(i32, i32),
}

/// CSS pseudo-class.
#[derive(Debug, Clone)]
pub enum PseudoClass {
    FirstChild,
    LastChild,
    OnlyChild,
    NthChild(NthArg),
    NthLastChild(NthArg),
    Not(Vec<SimpleSelector>),
    Empty,
    Root,
}

/// A single simple selector.
#[derive(Debug, Clone)]
pub enum SimpleSelector {
    Type(String),
    Universal,
    Class(String),
    Id(String),
    Attr(AttrSelector),
    Pseudo(PseudoClass),
}

/// Combinator between compound selectors.
#[derive(Debug, Clone, PartialEq)]
pub enum Combinator {
    Descendant,       // ' '  (whitespace)
    Child,            // >
    AdjacentSibling,  // +
    GeneralSibling,   // ~
}

/// A compound selector: multiple simple selectors on the same element.
#[derive(Debug, Clone)]
pub struct CompoundSelector(pub Vec<SimpleSelector>);

/// A complex selector: `compound (combinator compound)*`.
#[derive(Debug, Clone)]
pub struct ComplexSelector {
    pub head: CompoundSelector,
    /// Each entry is (combinator-from-left, next-compound).
    pub tail: Vec<(Combinator, CompoundSelector)>,
}

/// A comma-separated list of complex selectors.
pub type SelectorList = Vec<ComplexSelector>;

// ─── Parse error ──────────────────────────────────────────────────────────────

#[derive(Debug, Clone, PartialEq)]
pub struct SelectorError(pub String);

impl std::fmt::Display for SelectorError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "selector error: {}", self.0)
    }
}

impl std::error::Error for SelectorError {}

// ─── Parser ───────────────────────────────────────────────────────────────────

struct Parser<'a> {
    bytes: &'a [u8],
    pos: usize,
}

impl<'a> Parser<'a> {
    fn new(input: &'a str) -> Self {
        Parser { bytes: input.as_bytes(), pos: 0 }
    }

    fn eof(&self) -> bool { self.pos >= self.bytes.len() }

    fn peek(&self) -> Option<u8> { self.bytes.get(self.pos).copied() }

    fn next(&mut self) -> Option<u8> {
        let ch = self.bytes.get(self.pos).copied();
        if ch.is_some() { self.pos += 1; }
        ch
    }

    fn expect(&mut self, ch: u8) -> Result<(), SelectorError> {
        match self.peek() {
            Some(c) if c == ch => { self.pos += 1; Ok(()) }
            got => Err(SelectorError(format!(
                "expected '{}', got {:?} at pos {}",
                ch as char, got.map(|c| c as char), self.pos
            ))),
        }
    }

    fn skip_ws(&mut self) {
        while matches!(self.peek(), Some(b' ' | b'\t' | b'\n' | b'\r')) {
            self.pos += 1;
        }
    }

    /// Parse a CSS identifier.
    fn parse_ident(&mut self) -> Result<String, SelectorError> {
        let start = self.pos;
        // Allow leading '-'
        if self.peek() == Some(b'-') { self.pos += 1; }
        match self.peek() {
            Some(c) if c.is_ascii_alphabetic() || c == b'_' => { self.pos += 1; }
            _ => {
                self.pos = start;
                return Err(SelectorError(format!("expected identifier at pos {}", start)));
            }
        }
        while let Some(c) = self.peek() {
            if c.is_ascii_alphanumeric() || c == b'_' || c == b'-' { self.pos += 1; }
            else { break; }
        }
        let s = std::str::from_utf8(&self.bytes[start..self.pos]).unwrap_or("").to_string();
        if s.is_empty() {
            Err(SelectorError(format!("empty identifier at pos {start}")))
        } else {
            Ok(s)
        }
    }

    /// Parse a quoted string or an unquoted attribute value.
    ///
    /// CSS spec requires unquoted values to be valid idents, but we accept any
    /// non-`]` non-whitespace characters to handle common patterns like `.jpg`.
    fn parse_value(&mut self) -> Result<String, SelectorError> {
        match self.peek() {
            Some(b'"') | Some(b'\'') => {
                let quote = self.next().unwrap();
                let mut s = String::new();
                loop {
                    match self.next() {
                        None => return Err(SelectorError("unterminated string".into())),
                        Some(c) if c == quote => break,
                        Some(c) => s.push(c as char),
                    }
                }
                Ok(s)
            }
            _ => {
                // Unquoted: consume until ] or whitespace
                let start = self.pos;
                while let Some(c) = self.peek() {
                    if matches!(c, b']' | b' ' | b'\t' | b'\n' | b'\r') { break; }
                    self.pos += 1;
                }
                if self.pos == start {
                    return Err(SelectorError(format!("empty attribute value at pos {start}")));
                }
                Ok(std::str::from_utf8(&self.bytes[start..self.pos]).unwrap_or("").to_string())
            }
        }
    }

    fn parse_nth_arg(&mut self) -> Result<NthArg, SelectorError> {
        self.skip_ws();
        // Try keyword first
        let saved = self.pos;
        if let Ok(ident) = self.parse_ident() {
            match ident.to_lowercase().as_str() {
                "even" => return Ok(NthArg::Even),
                "odd"  => return Ok(NthArg::Odd),
                "n"    => {
                    let b = self.parse_nth_offset()?;
                    return Ok(NthArg::AnPlusB(1, b));
                }
                _ => {}
            }
        }
        self.pos = saved;

        // Parse optional sign
        let sign: i32 = match self.peek() {
            Some(b'-') => { self.pos += 1; -1 }
            Some(b'+') => { self.pos += 1;  1 }
            _          => 1,
        };

        // Collect digits
        let num_start = self.pos;
        while self.peek().map(|c| c.is_ascii_digit()).unwrap_or(false) {
            self.pos += 1;
        }
        let digits = std::str::from_utf8(&self.bytes[num_start..self.pos]).unwrap_or("");

        // Check for 'n'
        if matches!(self.peek(), Some(b'n') | Some(b'N')) {
            self.pos += 1; // consume 'n'
            let a: i32 = if digits.is_empty() { sign }
                         else { sign * digits.parse::<i32>().unwrap_or(0) };
            self.skip_ws();
            let b = self.parse_nth_offset()?;
            Ok(NthArg::AnPlusB(a, b))
        } else if !digits.is_empty() {
            let n = sign * digits.parse::<i32>().unwrap_or(1);
            Ok(NthArg::Index(n.max(1) as u32))
        } else {
            Err(SelectorError("invalid :nth-child argument".into()))
        }
    }

    fn parse_nth_offset(&mut self) -> Result<i32, SelectorError> {
        self.skip_ws();
        let b_sign: i32 = match self.peek() {
            Some(b'+') => { self.pos += 1; self.skip_ws(); 1  }
            Some(b'-') => { self.pos += 1; self.skip_ws(); -1 }
            _          => return Ok(0),
        };
        let b_start = self.pos;
        while self.peek().map(|c| c.is_ascii_digit()).unwrap_or(false) {
            self.pos += 1;
        }
        let b_str = std::str::from_utf8(&self.bytes[b_start..self.pos]).unwrap_or("0");
        Ok(b_sign * b_str.parse::<i32>().unwrap_or(0))
    }

    fn parse_pseudo(&mut self) -> Result<PseudoClass, SelectorError> {
        self.expect(b':')?;
        // Skip pseudo-elements (::before etc.)
        if self.peek() == Some(b':') {
            self.pos += 1;
            let _ = self.parse_ident();
            return Err(SelectorError("pseudo-elements not supported".into()));
        }
        let name = self.parse_ident()?.to_lowercase();
        match name.as_str() {
            "first-child"     => Ok(PseudoClass::FirstChild),
            "last-child"      => Ok(PseudoClass::LastChild),
            "only-child"      => Ok(PseudoClass::OnlyChild),
            "empty"           => Ok(PseudoClass::Empty),
            "root"            => Ok(PseudoClass::Root),
            "nth-child"       => {
                self.expect(b'(')?;
                let arg = self.parse_nth_arg()?;
                self.skip_ws();
                self.expect(b')')?;
                Ok(PseudoClass::NthChild(arg))
            }
            "nth-last-child"  => {
                self.expect(b'(')?;
                let arg = self.parse_nth_arg()?;
                self.skip_ws();
                self.expect(b')')?;
                Ok(PseudoClass::NthLastChild(arg))
            }
            "not" => {
                self.expect(b'(')?;
                self.skip_ws();
                let inner = self.parse_compound()?;
                self.skip_ws();
                self.expect(b')')?;
                Ok(PseudoClass::Not(inner.0))
            }
            other => Err(SelectorError(format!("unsupported pseudo-class :{other}"))),
        }
    }

    fn parse_attr_selector(&mut self) -> Result<AttrSelector, SelectorError> {
        self.expect(b'[')?;
        self.skip_ws();
        let name = self.parse_ident()?;
        self.skip_ws();
        let op_value = match self.peek() {
            Some(b']') => None,
            Some(b'=') => {
                self.pos += 1; self.skip_ws();
                Some((AttrOp::Exact, self.parse_value()?))
            }
            Some(b'~') => {
                self.pos += 1; self.expect(b'=')?; self.skip_ws();
                Some((AttrOp::Includes, self.parse_value()?))
            }
            Some(b'|') => {
                self.pos += 1; self.expect(b'=')?; self.skip_ws();
                Some((AttrOp::DashMatch, self.parse_value()?))
            }
            Some(b'^') => {
                self.pos += 1; self.expect(b'=')?; self.skip_ws();
                Some((AttrOp::Prefix, self.parse_value()?))
            }
            Some(b'$') => {
                self.pos += 1; self.expect(b'=')?; self.skip_ws();
                Some((AttrOp::Suffix, self.parse_value()?))
            }
            Some(b'*') => {
                self.pos += 1; self.expect(b'=')?; self.skip_ws();
                Some((AttrOp::Substring, self.parse_value()?))
            }
            got => return Err(SelectorError(format!(
                "unexpected '{}' in attribute selector", got.map(|c| c as char).unwrap_or('?')
            ))),
        };
        self.skip_ws();
        // Optional case-insensitivity flag 'i' or 's'
        if matches!(self.peek(), Some(b'i') | Some(b'I') | Some(b's') | Some(b'S')) {
            self.pos += 1;
            self.skip_ws();
        }
        self.expect(b']')?;
        Ok(AttrSelector { name, op_value })
    }

    fn parse_simple(&mut self) -> Result<SimpleSelector, SelectorError> {
        match self.peek() {
            Some(b'*') => { self.pos += 1; Ok(SimpleSelector::Universal) }
            Some(b'.') => {
                self.pos += 1;
                Ok(SimpleSelector::Class(self.parse_ident()?))
            }
            Some(b'#') => {
                self.pos += 1;
                // IDs can contain alphanumeric, underscore, hyphen
                let start = self.pos;
                while let Some(c) = self.peek() {
                    if c.is_ascii_alphanumeric() || c == b'_' || c == b'-' { self.pos += 1; }
                    else { break; }
                }
                if self.pos == start {
                    return Err(SelectorError("empty #id selector".into()));
                }
                let id = std::str::from_utf8(&self.bytes[start..self.pos]).unwrap_or("").to_string();
                Ok(SimpleSelector::Id(id))
            }
            Some(b'[') => Ok(SimpleSelector::Attr(self.parse_attr_selector()?)),
            Some(b':') => {
                let saved = self.pos;
                match self.parse_pseudo() {
                    Ok(p) => Ok(SimpleSelector::Pseudo(p)),
                    Err(e) => { self.pos = saved; Err(e) }
                }
            }
            Some(c) if c.is_ascii_alphabetic() || c == b'_' || c == b'-' => {
                Ok(SimpleSelector::Type(self.parse_ident()?))
            }
            got => Err(SelectorError(format!(
                "unexpected '{}' in selector at pos {}",
                got.map(|c| c as char).unwrap_or('?'), self.pos
            ))),
        }
    }

    /// Parse a compound selector (no whitespace between parts).
    fn parse_compound(&mut self) -> Result<CompoundSelector, SelectorError> {
        let mut parts = vec![];
        loop {
            // Stop at combinators, commas, closing parens, EOF
            match self.peek() {
                None | Some(b' ' | b'\t' | b'\n' | b'\r' | b'>' | b'+' | b'~' | b',' | b')') => break,
                _ => {}
            }
            match self.parse_simple() {
                Ok(s)  => parts.push(s),
                Err(_) => break,
            }
        }
        if parts.is_empty() {
            Err(SelectorError(format!("empty compound selector at pos {}", self.pos)))
        } else {
            Ok(CompoundSelector(parts))
        }
    }

    /// Peek at the combinator between compounds.  Returns `None` at end/comma.
    fn parse_combinator(&mut self) -> Option<Combinator> {
        let mut had_ws = false;
        while matches!(self.peek(), Some(b' ' | b'\t' | b'\n' | b'\r')) {
            had_ws = true;
            self.pos += 1;
        }
        match self.peek() {
            Some(b'>') => { self.pos += 1; self.skip_ws(); Some(Combinator::Child) }
            Some(b'+') => { self.pos += 1; self.skip_ws(); Some(Combinator::AdjacentSibling) }
            Some(b'~') => { self.pos += 1; self.skip_ws(); Some(Combinator::GeneralSibling) }
            None | Some(b',') | Some(b')') => None,
            _ if had_ws => Some(Combinator::Descendant),
            _ => None,
        }
    }

    fn parse_complex(&mut self) -> Result<ComplexSelector, SelectorError> {
        let head = self.parse_compound()?;
        let mut tail = vec![];
        loop {
            let saved = self.pos;
            match self.parse_combinator() {
                None => break,
                Some(comb) => match self.parse_compound() {
                    Ok(compound) => tail.push((comb, compound)),
                    Err(_)       => { self.pos = saved; break; }
                }
            }
        }
        Ok(ComplexSelector { head, tail })
    }

    fn parse_selector_list(&mut self) -> Result<SelectorList, SelectorError> {
        self.skip_ws();
        let mut list = vec![self.parse_complex()?];
        loop {
            self.skip_ws();
            if self.peek() != Some(b',') { break; }
            self.pos += 1;
            self.skip_ws();
            list.push(self.parse_complex()?);
        }
        Ok(list)
    }
}

/// Parse a CSS selector string.
pub fn parse_selector(input: &str) -> Result<SelectorList, SelectorError> {
    let mut p = Parser::new(input);
    let list = p.parse_selector_list()?;
    p.skip_ws();
    if !p.eof() {
        return Err(SelectorError(format!(
            "unexpected '{}' at pos {}",
            p.peek().map(|c| c as char).unwrap_or('?'), p.pos
        )));
    }
    Ok(list)
}

// ─── Matching ─────────────────────────────────────────────────────────────────

/// Returns `true` if `node_id` matches any selector in `list`.
pub fn matches_selector_list(doc: &Document, node_id: NodeId, list: &SelectorList) -> bool {
    list.iter().any(|sel| matches_complex(doc, node_id, sel))
}

fn matches_complex(doc: &Document, node_id: NodeId, sel: &ComplexSelector) -> bool {
    if sel.tail.is_empty() {
        return matches_compound(doc, node_id, &sel.head);
    }
    // Build left-to-right sequence: [head, c1, c2, …]
    // and combinators:              [comb1, comb2, …]
    // where combX is the combinator *before* the (X+1)-th compound.
    let compounds: Vec<&CompoundSelector> = std::iter::once(&sel.head)
        .chain(sel.tail.iter().map(|(_, c)| c))
        .collect();
    let combinators: Vec<&Combinator> = sel.tail.iter().map(|(c, _)| c).collect();

    // Rightmost compound must match current node.
    if !matches_compound(doc, node_id, compounds.last().unwrap()) {
        return false;
    }
    // Walk the rest right-to-left.
    matches_ancestors(doc, node_id, &compounds[..compounds.len() - 1], &combinators)
}

/// Recursively verify the left portion of a complex selector.
///
/// `compounds` contains the remaining (left) compounds to satisfy.
/// `combinators[i]` is the combinator *from* compounds[i] *to* compounds[i+1].
/// We need the last combinator to relate `node_id` to `compounds.last()`.
fn matches_ancestors(
    doc: &Document,
    node_id: NodeId,
    compounds: &[&CompoundSelector],
    combinators: &[&Combinator],
) -> bool {
    if compounds.is_empty() {
        return true;
    }
    let compound   = *compounds.last().unwrap();
    let combinator = *combinators.last().unwrap();
    let rest_c  = &compounds[..compounds.len() - 1];
    let rest_cb = &combinators[..combinators.len() - 1];

    match combinator {
        Combinator::Descendant => {
            let mut cur = doc.nodes[node_id].parent;
            while let Some(anc) = cur {
                if matches_compound(doc, anc, compound)
                    && matches_ancestors(doc, anc, rest_c, rest_cb)
                {
                    return true;
                }
                cur = doc.nodes[anc].parent;
            }
            false
        }
        Combinator::Child => {
            match doc.nodes[node_id].parent {
                Some(par) if matches_compound(doc, par, compound) => {
                    matches_ancestors(doc, par, rest_c, rest_cb)
                }
                _ => false,
            }
        }
        Combinator::AdjacentSibling => {
            let par = match doc.nodes[node_id].parent { Some(p) => p, None => return false };
            let sibs = &doc.nodes[par].children;
            let pos = match sibs.iter().position(|&c| c == node_id) { Some(p) => p, None => return false };
            if pos == 0 { return false; }
            let prev = sibs[pos - 1];
            matches_compound(doc, prev, compound)
                && matches_ancestors(doc, prev, rest_c, rest_cb)
        }
        Combinator::GeneralSibling => {
            let par = match doc.nodes[node_id].parent { Some(p) => p, None => return false };
            let sibs = &doc.nodes[par].children;
            let pos = match sibs.iter().position(|&c| c == node_id) { Some(p) => p, None => return false };
            sibs[..pos].iter().any(|&sib| {
                matches_compound(doc, sib, compound)
                    && matches_ancestors(doc, sib, rest_c, rest_cb)
            })
        }
    }
}

fn matches_compound(doc: &Document, node_id: NodeId, compound: &CompoundSelector) -> bool {
    compound.0.iter().all(|s| matches_simple(doc, node_id, s))
}

fn matches_simple(doc: &Document, node_id: NodeId, sel: &SimpleSelector) -> bool {
    let node = &doc.nodes[node_id];
    match sel {
        SimpleSelector::Universal => node.is_element(),
        SimpleSelector::Type(tag) => node.element_data()
            .map(|e| e.tag_name == *tag)
            .unwrap_or(false),
        SimpleSelector::Class(cls) => node.element_data()
            .and_then(|e| e.attr("class"))
            .map(|c| c.split_whitespace().any(|w| w == cls))
            .unwrap_or(false),
        SimpleSelector::Id(id) => node.element_data()
            .and_then(|e| e.attr("id"))
            .map(|v| v == id)
            .unwrap_or(false),
        SimpleSelector::Attr(attr_sel) => match node.element_data() {
            None => false,
            Some(e) => matches_attr(e, attr_sel),
        },
        SimpleSelector::Pseudo(pseudo) => matches_pseudo(doc, node_id, pseudo),
    }
}

fn matches_attr(e: &crate::html::dom::ElementData, sel: &AttrSelector) -> bool {
    let val = match e.attr(&sel.name) { Some(v) => v, None => return false };
    match &sel.op_value {
        None => true,
        Some((AttrOp::Exact,      exp)) => val == exp,
        Some((AttrOp::Includes,   word)) => val.split_whitespace().any(|w| w == word),
        Some((AttrOp::DashMatch,  pfx)) => val == pfx || val.starts_with(&format!("{pfx}-")),
        Some((AttrOp::Prefix,     pfx)) => val.starts_with(pfx.as_str()),
        Some((AttrOp::Suffix,     sfx)) => val.ends_with(sfx.as_str()),
        Some((AttrOp::Substring,  sub)) => val.contains(sub.as_str()),
    }
}

fn matches_pseudo(doc: &Document, node_id: NodeId, pseudo: &PseudoClass) -> bool {
    match pseudo {
        PseudoClass::Root => {
            doc.nodes[node_id].parent == Some(DOCUMENT_NODE_ID)
                && doc.nodes[node_id].element_data()
                    .map(|e| e.tag_name == "html")
                    .unwrap_or(false)
        }
        PseudoClass::Empty => {
            doc.nodes[node_id].is_element()
                && doc.nodes[node_id].children.iter().all(|&c| {
                    matches!(&doc.nodes[c].data, NodeData::Text(t) if t.trim().is_empty())
                        || matches!(&doc.nodes[c].data, NodeData::Comment(_))
                })
        }
        PseudoClass::FirstChild => {
            let par = match doc.nodes[node_id].parent { Some(p) => p, None => return false };
            doc.nodes[par].children.first() == Some(&node_id)
        }
        PseudoClass::LastChild => {
            let par = match doc.nodes[node_id].parent { Some(p) => p, None => return false };
            doc.nodes[par].children.last() == Some(&node_id)
        }
        PseudoClass::OnlyChild => {
            let par = match doc.nodes[node_id].parent { Some(p) => p, None => return false };
            let elems: Vec<_> = doc.nodes[par].children.iter()
                .filter(|&&c| doc.nodes[c].is_element())
                .collect();
            elems.len() == 1 && *elems[0] == node_id
        }
        PseudoClass::NthChild(arg)     => nth_match(doc, node_id, arg, false),
        PseudoClass::NthLastChild(arg) => nth_match(doc, node_id, arg, true),
        PseudoClass::Not(inner) => {
            // :not() is true if the element does NOT match the inner compound.
            !inner.iter().all(|s| matches_simple(doc, node_id, s))
        }
    }
}

fn nth_match(doc: &Document, node_id: NodeId, arg: &NthArg, from_end: bool) -> bool {
    let par = match doc.nodes[node_id].parent { Some(p) => p, None => return false };
    let elems: Vec<NodeId> = doc.nodes[par].children.iter()
        .filter(|&&c| doc.nodes[c].is_element())
        .copied()
        .collect();
    let pos = match elems.iter().position(|&c| c == node_id) { Some(p) => p, None => return false };
    // 1-based index
    let n = if from_end { elems.len() - pos } else { pos + 1 } as i32;
    match arg {
        NthArg::Even => n % 2 == 0,
        NthArg::Odd  => n % 2 == 1,
        NthArg::Index(i) => n == *i as i32,
        NthArg::AnPlusB(a, b) => {
            if *a == 0 { return n == *b; }
            let diff = n - b;
            if *a > 0 { diff >= 0 && diff % a == 0 }
            else      { diff <= 0 && diff % a == 0 }
        }
    }
}

// ─── Public query API ─────────────────────────────────────────────────────────

/// Find the first descendant of `context` that matches `selector`.
pub fn query_selector(
    doc: &Document,
    context: NodeId,
    selector: &str,
) -> Result<Option<NodeId>, SelectorError> {
    let list = parse_selector(selector)?;
    Ok(query_selector_with(doc, context, &list))
}

/// Find all descendants of `context` that match `selector`.
pub fn query_selector_all(
    doc: &Document,
    context: NodeId,
    selector: &str,
) -> Result<Vec<NodeId>, SelectorError> {
    let list = parse_selector(selector)?;
    Ok(query_selector_all_with(doc, context, &list))
}

pub fn query_selector_with(
    doc: &Document,
    context: NodeId,
    list: &SelectorList,
) -> Option<NodeId> {
    let mut q = VecDeque::new();
    for &c in &doc.nodes[context].children { q.push_back(c); }
    while let Some(id) = q.pop_front() {
        if matches_selector_list(doc, id, list) { return Some(id); }
        for &c in &doc.nodes[id].children { q.push_back(c); }
    }
    None
}

pub fn query_selector_all_with(
    doc: &Document,
    context: NodeId,
    list: &SelectorList,
) -> Vec<NodeId> {
    let mut result = vec![];
    let mut q = VecDeque::new();
    for &c in &doc.nodes[context].children { q.push_back(c); }
    while let Some(id) = q.pop_front() {
        if matches_selector_list(doc, id, list) { result.push(id); }
        for &c in &doc.nodes[id].children { q.push_back(c); }
    }
    result
}
