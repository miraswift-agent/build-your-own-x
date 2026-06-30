//! HTML5-compliant tokenizer.
//!
//! Implements the tokenization algorithm from the HTML5 spec (§13.2.5),
//! covering the states needed for real-world HTML:
//!
//! * Start / end / self-closing tags with full attribute parsing
//! * `<!-- ... -->` comments (including `--!>` and `<!---` quirks)
//! * `<!DOCTYPE html>` declarations
//! * Raw text elements (`<script>`, `<style>`) — content returned verbatim
//! * Escapable raw text elements (`<textarea>`, `<title>`) — same but entities decoded
//! * Character entity references (`&amp;`, `&#123;`, `&#xAB;`)
//!
//! Error recovery follows the spec: parse errors are recorded but tokenization
//! continues, producing the best possible token stream from malformed input.

use crate::html::dom::Attribute;

// ---------------------------------------------------------------------------
// Token
// ---------------------------------------------------------------------------

/// A single HTML token.
#[derive(Debug, Clone, PartialEq)]
pub enum Token {
    Doctype {
        name: String,
        public_id: Option<String>,
        system_id: Option<String>,
        force_quirks: bool,
    },
    StartTag {
        name: String,
        attrs: Vec<Attribute>,
        self_closing: bool,
    },
    EndTag {
        name: String,
    },
    Comment(String),
    /// Contiguous characters (may be whitespace-only).
    Text(String),
    Eof,
}

// ---------------------------------------------------------------------------
// Tokenizer state
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, PartialEq)]
enum State {
    Data,
    /// Inside a raw text element (`script`, `style`).
    RawText,
    /// Inside an escapable raw text element (`textarea`, `title`).
    RCData,
    TagOpen,
    EndTagOpen,
    TagName,
    SelfClosingStartTag,
    BeforeAttributeName,
    AttributeName,
    AfterAttributeName,
    BeforeAttributeValue,
    AttributeValueDoubleQuoted,
    AttributeValueSingleQuoted,
    AttributeValueUnquoted,
    AfterAttributeValueQuoted,
    MarkupDeclarationOpen,
    CommentStart,
    CommentStartDash,
    Comment,
    CommentEndDash,
    CommentEnd,
    CommentEndBang,
    Doctype,
    BeforeDoctypeName,
    DoctypeName,
    AfterDoctypeName,
    AfterDoctypePublicKeyword,
    BeforeDoctypePublicId,
    DoctypePublicIdDoubleQuoted,
    DoctypePublicIdSingleQuoted,
    AfterDoctypePublicId,
    BetweenDoctypePublicAndSystemIds,
    AfterDoctypeSystemKeyword,
    BeforeDoctypeSystemId,
    DoctypeSystemIdDoubleQuoted,
    DoctypeSystemIdSingleQuoted,
    AfterDoctypeSystemId,
    BogusDoctype,
    /// Raw-text end-tag detection: reading `</` potentially followed by end tag name.
    /// We buffer the chars seen so far so we can emit them as text if no match.
    RawTextLessThanSign,
    RawTextEndTagOpen,
    RawTextEndTagName,
    /// Same as above but for RCData.
    RCDataLessThanSign,
    RCDataEndTagOpen,
    RCDataEndTagName,
}

// ---------------------------------------------------------------------------
// Tag / attribute builders (transient, emptied when a tag is emitted)
// ---------------------------------------------------------------------------

#[derive(Default)]
struct TagBuilder {
    name: String,
    attrs: Vec<Attribute>,
    self_closing: bool,
    is_end: bool,
}

#[derive(Default)]
struct AttrBuilder {
    name: String,
    value: String,
}

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------

/// Converts an HTML byte string into a flat sequence of [`Token`]s.
    pub struct Tokenizer {
    input: Vec<char>,
    pos: usize,
    state: State,
    /// State to return to after raw text / rcdata end-tag detection fails.
    return_state: State,
    /// The tag name we are looking for to end a raw text / rcdata block.
    raw_end_tag: String,
    /// Characters buffered during raw-text end-tag scanning (may be emitted
    /// as text if the candidate end tag doesn't match).
    raw_buf: String,
    /// Pending tokens to emit before resuming normal processing.
    /// Used when exiting raw text mode: we need to emit both accumulated
    /// text AND the closing end tag, but step() can only return one token.
    pending: Vec<Token>,
    /// Current partially-built tag.
    tag: TagBuilder,
    /// Current partially-built attribute.
    attr: AttrBuilder,
    /// Accumulated text content.
    text: String,
    /// Comment content buffer.
    comment: String,
    /// DOCTYPE name/public-id/system-id buffers.
    doctype_name: String,
    doctype_public: Option<String>,
    doctype_system: Option<String>,
    doctype_force_quirks: bool,
    /// Parse errors — non-fatal, collected for diagnostics.
    pub errors: Vec<String>,
}

impl Tokenizer {
    pub fn new(input: &str) -> Self {
        Tokenizer {
            input: input.chars().collect(),
            pos: 0,
            state: State::Data,
            return_state: State::Data,
            raw_end_tag: String::new(),
            raw_buf: String::new(),
            pending: Vec::new(),
            tag: TagBuilder::default(),
            attr: AttrBuilder::default(),
            text: String::new(),
            comment: String::new(),
            doctype_name: String::new(),
            doctype_public: None,
            doctype_system: None,
            doctype_force_quirks: false,
            errors: vec![],
        }
    }

    /// Tokenize the entire input and return all tokens (including the final
    /// `Eof` sentinel).
    pub fn tokenize(mut self) -> (Vec<Token>, Vec<String>) {
        let mut tokens = Vec::new();
        loop {
            // Drain pending tokens first
            if let Some(tok) = self.pending.pop() {
                let done = matches!(tok, Token::Eof);
                tokens.push(tok);
                if done { break; }
                continue;
            }
            let emitted = self.step();
            let done = matches!(emitted, Some(Token::Eof));
            if let Some(tok) = emitted {
                tokens.push(tok);
            }
            if done { break; }
        }
        let errors = self.errors.clone();
        (tokens, errors)
    }

    // ------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------

    fn current(&self) -> Option<char> {
        self.input.get(self.pos).copied()
    }

    fn peek_str(&self, s: &str) -> bool {
        let chars: Vec<char> = s.chars().collect();
        self.input[self.pos..].starts_with(&chars)
    }

    fn peek_str_ci(&self, s: &str) -> bool {
        let haystack: String = self.input[self.pos..].iter()
            .take(s.len())
            .collect::<String>()
            .to_ascii_uppercase();
        haystack == s.to_ascii_uppercase()
    }

    fn advance(&mut self) { self.pos += 1; }

    fn advance_by(&mut self, n: usize) { self.pos += n; }

    fn parse_error(&mut self, msg: &str) {
        self.errors.push(format!("pos {}: {}", self.pos, msg));
    }

    // ------------------------------------------------------------------
    // Attribute finalization
    // ------------------------------------------------------------------

    fn finish_attribute(&mut self) {
        if !self.attr.name.is_empty() || !self.attr.value.is_empty() {
            let name = std::mem::take(&mut self.attr.name);
            let value = std::mem::take(&mut self.attr.value);
            // Deduplicate: only keep first occurrence of each attribute name.
            if !self.tag.attrs.iter().any(|a| a.name == name) {
                self.tag.attrs.push(Attribute { name, value });
            }
        }
        self.attr = AttrBuilder::default();
    }

    // ------------------------------------------------------------------
    // Token emission helpers
    // ------------------------------------------------------------------

    fn emit_text(&mut self) -> Option<Token> {
        if self.text.is_empty() { return None; }
        let t = std::mem::take(&mut self.text);
        Some(Token::Text(t))
    }

    fn emit_tag(&mut self) -> Token {
        self.finish_attribute();
        let name = std::mem::take(&mut self.tag.name);
        let attrs = std::mem::take(&mut self.tag.attrs);
        let self_closing = self.tag.self_closing;
        let is_end = self.tag.is_end;
        self.tag = TagBuilder::default();
        if is_end {
            Token::EndTag { name }
        } else {
            Token::StartTag { name, attrs, self_closing }
        }
    }

    fn emit_comment(&mut self) -> Token {
        let c = std::mem::take(&mut self.comment);
        Token::Comment(c)
    }

    fn emit_doctype(&mut self) -> Token {
        let name = std::mem::take(&mut self.doctype_name);
        let public_id = self.doctype_public.take();
        let system_id = self.doctype_system.take();
        let force_quirks = self.doctype_force_quirks;
        self.doctype_force_quirks = false;
        Token::Doctype { name, public_id, system_id, force_quirks }
    }

    // ------------------------------------------------------------------
    // Character entity reference decoder (minimal, common subset)
    // ------------------------------------------------------------------

    /// Attempt to consume an entity reference starting at `self.pos` (which
    /// should be at the char *after* `&`). Returns the decoded char(s) and
    /// advances pos past the entity. Returns `None` if no entity found,
    /// in which case `&` should be emitted as-is.
    fn consume_entity(&mut self) -> Option<String> {
        let start = self.pos;

        // Numeric character reference: &#NNN; or &#xHH;
        if self.current() == Some('#') {
            self.advance();
            let hex = self.current() == Some('x') || self.current() == Some('X');
            if hex { self.advance(); }
            let num_start = self.pos;
            while let Some(c) = self.current() {
                if (hex && c.is_ascii_hexdigit()) || (!hex && c.is_ascii_digit()) {
                    self.advance();
                } else { break; }
            }
            if self.pos == num_start {
                // No digits — not a valid entity, backtrack
                self.pos = start;
                return None;
            }
            let digits: String = self.input[num_start..self.pos].iter().collect();
            // Consume optional semicolon
            if self.current() == Some(';') { self.advance(); }
            let code = if hex {
                u32::from_str_radix(&digits, 16).ok()?
            } else {
                digits.parse::<u32>().ok()?
            };
            return char::from_u32(code).map(|c| c.to_string())
                .or_else(|| Some('\u{FFFD}'.to_string()));
        }

        // Named character reference — handle the most common ones
        // We look for `name;` starting at self.pos
        let remaining: String = self.input[self.pos..].iter()
            .take(32) // max named entity length
            .collect();

        let (decoded, consumed) = match_named_entity(&remaining)?;
        self.advance_by(consumed);
        Some(decoded.to_string())
    }

    // ------------------------------------------------------------------
    // Main step function — processes one character and returns an optional
    // token (text tokens may be held until flushed).
    // ------------------------------------------------------------------

    fn step(&mut self) -> Option<Token> {
        loop {
            let c = self.current();
            match &self.state {

                // ── Data ────────────────────────────────────────────────
                State::Data => {
                    match c {
                        None => {
                            // EOF
                            if let Some(t) = self.emit_text() {
                                self.state = State::Data;
                                return Some(t);
                            }
                            return Some(Token::Eof);
                        }
                        Some('<') => {
                            self.advance();
                            if let Some(t) = self.emit_text() {
                                self.state = State::TagOpen;
                                return Some(t);
                            }
                            self.state = State::TagOpen;
                        }
                        Some('&') => {
                            self.advance();
                            let start = self.pos;
                            if let Some(decoded) = self.consume_entity() {
                                self.text.push_str(&decoded);
                            } else {
                                self.pos = start;
                                self.text.push('&');
                            }
                        }
                        Some(ch) => {
                            self.text.push(ch);
                            self.advance();
                        }
                    }
                }

                // ── RawText (script/style) ───────────────────────────────
                State::RawText => {
                    match c {
                        None => {
                            if let Some(t) = self.emit_text() {
                                self.state = State::Data;
                                return Some(t);
                            }
                            return Some(Token::Eof);
                        }
                        Some('<') => {
                            self.advance();
                            self.raw_buf.clear();
                            self.raw_buf.push('<');
                            self.state = State::RawTextLessThanSign;
                        }
                        Some(ch) => {
                            self.text.push(ch);
                            self.advance();
                        }
                    }
                }

                // ── RawTextLessThanSign ──────────────────────────────────
                State::RawTextLessThanSign => {
                    match c {
                        Some('/') => {
                            self.advance();
                            self.raw_buf.push('/');
                            self.state = State::RawTextEndTagOpen;
                        }
                        _ => {
                            // Not a close tag — emit buffered chars as text
                            let buf = std::mem::take(&mut self.raw_buf);
                            self.text.push_str(&buf);
                            self.state = State::RawText;
                        }
                    }
                }

                // ── RawTextEndTagOpen ────────────────────────────────────
                State::RawTextEndTagOpen => {
                    match c {
                        Some(ch) if ch.is_ascii_alphabetic() => {
                            self.tag = TagBuilder { is_end: true, ..Default::default() };
                            self.tag.name.push(ch.to_ascii_lowercase());
                            self.raw_buf.push(ch.to_ascii_lowercase());
                            self.advance();
                            self.state = State::RawTextEndTagName;
                        }
                        _ => {
                            let buf = std::mem::take(&mut self.raw_buf);
                            self.text.push_str(&buf);
                            self.state = State::RawText;
                        }
                    }
                }

                // ── RawTextEndTagName ────────────────────────────────────
                State::RawTextEndTagName => {
                    match c {
                        Some(ch) if ch.is_ascii_alphanumeric() || ch == '-' || ch == '_' => {
                            self.tag.name.push(ch.to_ascii_lowercase());
                            self.raw_buf.push(ch);
                            self.advance();
                        }
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => {
                            if self.tag.name == self.raw_end_tag {
                                // Matching end tag — flush text, then process attributes
                                let text = self.emit_text();
                                self.state = State::BeforeAttributeName;
                                if text.is_some() { return text; }
                                continue;
                            } else {
                                let buf = std::mem::take(&mut self.raw_buf);
                                self.text.push_str(&buf);
                                self.tag = TagBuilder::default();
                                self.state = State::RawText;
                            }
                        }
                        Some('>') => {
                            if self.tag.name == self.raw_end_tag {
                                // Valid end tag — exit raw text mode
                                self.advance();
                                self.raw_end_tag.clear();
                                let text = self.emit_text();
                                let tag = self.emit_tag();
                                self.state = State::Data;
                                // Queue: text (if any) then end tag
                                self.pending.push(tag);
                                if text.is_some() {
                                    return text;
                                }
                                continue;
                            } else {
                                let buf = std::mem::take(&mut self.raw_buf);
                                self.text.push_str(&buf);
                                self.tag = TagBuilder::default();
                                self.state = State::RawText;
                            }
                        }
                        Some('/') => {
                            if self.tag.name == self.raw_end_tag {
                                self.advance();
                                self.state = State::SelfClosingStartTag;
                            } else {
                                let buf = std::mem::take(&mut self.raw_buf);
                                self.text.push_str(&buf);
                                self.tag = TagBuilder::default();
                                self.state = State::RawText;
                            }
                        }
                        None => {
                            let buf = std::mem::take(&mut self.raw_buf);
                            self.text.push_str(&buf);
                            self.tag = TagBuilder::default();
                            if let Some(t) = self.emit_text() {
                                self.state = State::Data;
                                return Some(t);
                            }
                            return Some(Token::Eof);
                        }
                        Some(_) => {
                            let buf = std::mem::take(&mut self.raw_buf);
                            self.text.push_str(&buf);
                            self.tag = TagBuilder::default();
                            self.state = State::RawText;
                        }
                    }
                }

                // ── RCData (textarea / title) ────────────────────────────
                State::RCData => {
                    match c {
                        None => {
                            if let Some(t) = self.emit_text() {
                                self.state = State::Data;
                                return Some(t);
                            }
                            return Some(Token::Eof);
                        }
                        Some('&') => {
                            self.advance();
                            let start = self.pos;
                            if let Some(decoded) = self.consume_entity() {
                                self.text.push_str(&decoded);
                            } else {
                                self.pos = start;
                                self.text.push('&');
                            }
                        }
                        Some('<') => {
                            self.advance();
                            self.raw_buf.clear();
                            self.raw_buf.push('<');
                            self.state = State::RCDataLessThanSign;
                        }
                        Some(ch) => {
                            self.text.push(ch);
                            self.advance();
                        }
                    }
                }

                State::RCDataLessThanSign => {
                    match c {
                        Some('/') => {
                            self.advance();
                            self.raw_buf.push('/');
                            self.state = State::RCDataEndTagOpen;
                        }
                        _ => {
                            let buf = std::mem::take(&mut self.raw_buf);
                            self.text.push_str(&buf);
                            self.state = State::RCData;
                        }
                    }
                }

                State::RCDataEndTagOpen => {
                    match c {
                        Some(ch) if ch.is_ascii_alphabetic() => {
                            self.tag = TagBuilder { is_end: true, ..Default::default() };
                            self.tag.name.push(ch.to_ascii_lowercase());
                            self.raw_buf.push(ch.to_ascii_lowercase());
                            self.advance();
                            self.state = State::RCDataEndTagName;
                        }
                        _ => {
                            let buf = std::mem::take(&mut self.raw_buf);
                            self.text.push_str(&buf);
                            self.state = State::RCData;
                        }
                    }
                }

                State::RCDataEndTagName => {
                    match c {
                        Some(ch) if ch.is_ascii_alphanumeric() || ch == '-' || ch == '_' => {
                            self.tag.name.push(ch.to_ascii_lowercase());
                            self.raw_buf.push(ch);
                            self.advance();
                        }
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => {
                            // Whitespace after end tag name
                            if self.tag.name == self.raw_end_tag {
                                // Matching end tag — flush text, then process attributes
                                let text = self.emit_text();
                                self.state = State::BeforeAttributeName;
                                if text.is_some() { return text; }
                                continue; // no text, reconsume in BeforeAttributeName
                            } else {
                                let buf = std::mem::take(&mut self.raw_buf);
                                self.text.push_str(&buf);
                                self.tag = TagBuilder::default();
                                self.state = State::RCData;
                            }
                        }
                        Some('>') => {
                            if self.tag.name == self.raw_end_tag {
                                // Valid end tag — exit RCData mode
                                self.advance();
                                self.raw_end_tag.clear();
                                let text = self.emit_text();
                                let tag = self.emit_tag();
                                self.state = State::Data;
                                // Queue: text (if any) then end tag
                                self.pending.push(tag);
                                if text.is_some() {
                                    return text;
                                }
                                // No text, return the tag from pending next iteration
                                continue;
                            } else {
                                let buf = std::mem::take(&mut self.raw_buf);
                                self.text.push_str(&buf);
                                self.tag = TagBuilder::default();
                                self.state = State::RCData;
                            }
                        }
                        Some('/') => {
                            if self.tag.name == self.raw_end_tag {
                                // Self-closing end tag — parse error, but process as normal
                                self.advance();
                                self.state = State::SelfClosingStartTag;
                            } else {
                                let buf = std::mem::take(&mut self.raw_buf);
                                self.text.push_str(&buf);
                                self.tag = TagBuilder::default();
                                self.state = State::RCData;
                            }
                        }
                        None => {
                            // EOF in end tag name
                            let buf = std::mem::take(&mut self.raw_buf);
                            self.text.push_str(&buf);
                            self.tag = TagBuilder::default();
                            if let Some(t) = self.emit_text() {
                                self.state = State::Data;
                                return Some(t);
                            }
                            return Some(Token::Eof);
                        }
                        Some(_) => {
                            let buf = std::mem::take(&mut self.raw_buf);
                            self.text.push_str(&buf);
                            self.tag = TagBuilder::default();
                            self.state = State::RCData;
                        }
                    }
                }

                // ── TagOpen ──────────────────────────────────────────────
                State::TagOpen => {
                    match c {
                        Some('/') => {
                            self.advance();
                            self.state = State::EndTagOpen;
                        }
                        Some(ch) if ch.is_ascii_alphabetic() => {
                            self.tag = TagBuilder { is_end: false, ..Default::default() };
                            self.tag.name.push(ch.to_ascii_lowercase());
                            self.advance();
                            self.state = State::TagName;
                        }
                        Some('!') => {
                            self.advance();
                            self.state = State::MarkupDeclarationOpen;
                        }
                        Some('?') => {
                            // Processing instruction — treat as bogus comment
                            self.parse_error("unexpected-question-mark-instead-of-tag-name");
                            self.comment.clear();
                            // consume until >
                            while let Some(ch) = self.current() {
                                self.advance();
                                if ch == '>' { break; }
                                self.comment.push(ch);
                            }
                            return Some(self.emit_comment());
                        }
                        None => {
                            self.parse_error("eof-before-tag-name");
                            self.text.push('<');
                            if let Some(t) = self.emit_text() {
                                self.state = State::Data; // reset state so next call hits Data/EOF
                                return Some(t);
                            }
                            return Some(Token::Eof);
                        }
                        Some(_) => {
                            self.parse_error("invalid-first-character-of-tag-name");
                            self.text.push('<');
                            self.state = State::Data;
                            // reconsume
                        }
                    }
                }

                // ── EndTagOpen ───────────────────────────────────────────
                State::EndTagOpen => {
                    match c {
                        Some(ch) if ch.is_ascii_alphabetic() => {
                            self.tag = TagBuilder { is_end: true, ..Default::default() };
                            self.tag.name.push(ch.to_ascii_lowercase());
                            self.advance();
                            self.state = State::TagName;
                        }
                        Some('>') => {
                            self.parse_error("missing-end-tag-name");
                            self.advance();
                            self.state = State::Data;
                        }
                        None => {
                            self.parse_error("eof-before-tag-name");
                            self.text.push_str("</");
                            if let Some(t) = self.emit_text() {
                                self.state = State::Data; // reset to avoid infinite loop
                                return Some(t);
                            }
                            return Some(Token::Eof);
                        }
                        Some(_) => {
                            self.parse_error("invalid-first-character-of-tag-name");
                            // Treat as bogus comment
                            self.comment.clear();
                            while let Some(ch) = self.current() {
                                self.advance();
                                if ch == '>' { break; }
                                self.comment.push(ch);
                            }
                            self.state = State::Data;
                            return Some(self.emit_comment());
                        }
                    }
                }

                // ── TagName ──────────────────────────────────────────────
                State::TagName => {
                    match c {
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => {
                            self.advance();
                            self.state = State::BeforeAttributeName;
                        }
                        Some('/') => {
                            self.advance();
                            self.state = State::SelfClosingStartTag;
                        }
                        Some('>') => {
                            self.advance();
                            let tok = self.emit_tag_and_set_raw_state();
                            self.state = self.next_state_after_tag();
                            return Some(tok);
                        }
                        None => {
                            self.parse_error("eof-in-tag");
                            return Some(Token::Eof);
                        }
                        Some('\0') => {
                            self.parse_error("unexpected-null-character");
                            self.tag.name.push('\u{FFFD}');
                            self.advance();
                        }
                        Some(ch) => {
                            self.tag.name.push(ch.to_ascii_lowercase());
                            self.advance();
                        }
                    }
                }

                // ── SelfClosingStartTag ──────────────────────────────────
                State::SelfClosingStartTag => {
                    match c {
                        Some('>') => {
                            self.advance();
                            self.tag.self_closing = true;
                            let tok = self.emit_tag();
                            self.state = State::Data;
                            return Some(tok);
                        }
                        None => {
                            self.parse_error("eof-in-tag");
                            return Some(Token::Eof);
                        }
                        Some(_) => {
                            self.parse_error("unexpected-solidus-in-tag");
                            self.state = State::BeforeAttributeName;
                            // reconsume
                        }
                    }
                }

                // ── BeforeAttributeName ──────────────────────────────────
                State::BeforeAttributeName => {
                    match c {
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => {
                            self.advance();
                        }
                        Some('/') | Some('>') | None => {
                            self.state = State::AfterAttributeName;
                        }
                        Some('=') => {
                            self.parse_error("unexpected-equals-sign-before-attribute-name");
                            self.attr.name.push('=');
                            self.advance();
                            self.state = State::AttributeName;
                        }
                        Some(_) => {
                            self.finish_attribute();
                            self.state = State::AttributeName;
                        }
                    }
                }

                // ── AttributeName ────────────────────────────────────────
                State::AttributeName => {
                    match c {
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => {
                            self.advance();
                            self.state = State::AfterAttributeName;
                        }
                        Some('/') | Some('>') | None => {
                            self.state = State::AfterAttributeName;
                        }
                        Some('=') => {
                            self.advance();
                            self.state = State::BeforeAttributeValue;
                        }
                        Some('\0') => {
                            self.parse_error("unexpected-null-character");
                            self.attr.name.push('\u{FFFD}');
                            self.advance();
                        }
                        Some('"') | Some('\'') | Some('<') => {
                            self.parse_error("unexpected-character-in-attribute-name");
                            let ch = c.unwrap();
                            self.attr.name.push(ch.to_ascii_lowercase());
                            self.advance();
                        }
                        Some(ch) => {
                            self.attr.name.push(ch.to_ascii_lowercase());
                            self.advance();
                        }
                    }
                }

                // ── AfterAttributeName ───────────────────────────────────
                State::AfterAttributeName => {
                    match c {
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => {
                            self.advance();
                        }
                        Some('/') => {
                            self.finish_attribute();
                            self.advance();
                            self.state = State::SelfClosingStartTag;
                        }
                        Some('=') => {
                            self.advance();
                            self.state = State::BeforeAttributeValue;
                        }
                        Some('>') => {
                            self.finish_attribute();
                            self.advance();
                            let tok = self.emit_tag_and_set_raw_state();
                            self.state = self.next_state_after_tag();
                            return Some(tok);
                        }
                        None => {
                            self.parse_error("eof-in-tag");
                            self.finish_attribute();
                            return Some(Token::Eof);
                        }
                        Some(_) => {
                            self.finish_attribute();
                            self.state = State::AttributeName;
                        }
                    }
                }

                // ── BeforeAttributeValue ─────────────────────────────────
                State::BeforeAttributeValue => {
                    match c {
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => {
                            self.advance();
                        }
                        Some('"') => {
                            self.advance();
                            self.state = State::AttributeValueDoubleQuoted;
                        }
                        Some('\'') => {
                            self.advance();
                            self.state = State::AttributeValueSingleQuoted;
                        }
                        Some('>') => {
                            self.parse_error("missing-attribute-value");
                            self.finish_attribute();
                            self.advance();
                            let tok = self.emit_tag_and_set_raw_state();
                            self.state = self.next_state_after_tag();
                            return Some(tok);
                        }
                        _ => {
                            self.state = State::AttributeValueUnquoted;
                            // reconsume
                        }
                    }
                }

                // ── AttributeValueDoubleQuoted ───────────────────────────
                State::AttributeValueDoubleQuoted => {
                    match c {
                        Some('"') => {
                            self.advance();
                            self.state = State::AfterAttributeValueQuoted;
                        }
                        Some('&') => {
                            self.advance();
                            let start = self.pos;
                            if let Some(decoded) = self.consume_entity() {
                                self.attr.value.push_str(&decoded);
                            } else {
                                self.pos = start;
                                self.attr.value.push('&');
                            }
                        }
                        Some('\0') => {
                            self.parse_error("unexpected-null-character");
                            self.attr.value.push('\u{FFFD}');
                            self.advance();
                        }
                        None => {
                            self.parse_error("eof-in-tag");
                            self.finish_attribute();
                            return Some(Token::Eof);
                        }
                        Some(ch) => {
                            self.attr.value.push(ch);
                            self.advance();
                        }
                    }
                }

                // ── AttributeValueSingleQuoted ───────────────────────────
                State::AttributeValueSingleQuoted => {
                    match c {
                        Some('\'') => {
                            self.advance();
                            self.state = State::AfterAttributeValueQuoted;
                        }
                        Some('&') => {
                            self.advance();
                            let start = self.pos;
                            if let Some(decoded) = self.consume_entity() {
                                self.attr.value.push_str(&decoded);
                            } else {
                                self.pos = start;
                                self.attr.value.push('&');
                            }
                        }
                        Some('\0') => {
                            self.parse_error("unexpected-null-character");
                            self.attr.value.push('\u{FFFD}');
                            self.advance();
                        }
                        None => {
                            self.parse_error("eof-in-tag");
                            self.finish_attribute();
                            return Some(Token::Eof);
                        }
                        Some(ch) => {
                            self.attr.value.push(ch);
                            self.advance();
                        }
                    }
                }

                // ── AttributeValueUnquoted ───────────────────────────────
                State::AttributeValueUnquoted => {
                    match c {
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => {
                            self.finish_attribute();
                            self.advance();
                            self.state = State::BeforeAttributeName;
                        }
                        Some('&') => {
                            self.advance();
                            let start = self.pos;
                            if let Some(decoded) = self.consume_entity() {
                                self.attr.value.push_str(&decoded);
                            } else {
                                self.pos = start;
                                self.attr.value.push('&');
                            }
                        }
                        Some('>') => {
                            self.finish_attribute();
                            self.advance();
                            let tok = self.emit_tag_and_set_raw_state();
                            self.state = self.next_state_after_tag();
                            return Some(tok);
                        }
                        Some('\0') => {
                            self.parse_error("unexpected-null-character");
                            self.attr.value.push('\u{FFFD}');
                            self.advance();
                        }
                        Some('"') | Some('\'') | Some('<') | Some('=') | Some('`') => {
                            self.parse_error("unexpected-character-in-unquoted-attribute-value");
                            let ch = c.unwrap();
                            self.attr.value.push(ch);
                            self.advance();
                        }
                        None => {
                            self.parse_error("eof-in-tag");
                            self.finish_attribute();
                            return Some(Token::Eof);
                        }
                        Some(ch) => {
                            self.attr.value.push(ch);
                            self.advance();
                        }
                    }
                }

                // ── AfterAttributeValueQuoted ────────────────────────────
                State::AfterAttributeValueQuoted => {
                    match c {
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => {
                            self.advance();
                            self.state = State::BeforeAttributeName;
                        }
                        Some('/') => {
                            self.advance();
                            self.state = State::SelfClosingStartTag;
                        }
                        Some('>') => {
                            self.advance();
                            let tok = self.emit_tag_and_set_raw_state();
                            self.state = self.next_state_after_tag();
                            return Some(tok);
                        }
                        None => {
                            self.parse_error("eof-in-tag");
                            return Some(Token::Eof);
                        }
                        Some(_) => {
                            self.parse_error("missing-whitespace-between-attributes");
                            self.state = State::BeforeAttributeName;
                            // reconsume
                        }
                    }
                }

                // ── MarkupDeclarationOpen ────────────────────────────────
                State::MarkupDeclarationOpen => {
                    if self.peek_str("--") {
                        self.advance_by(2);
                        self.comment.clear();
                        self.state = State::CommentStart;
                    } else if self.peek_str_ci("DOCTYPE") {
                        self.advance_by(7);
                        self.state = State::Doctype;
                    } else if self.peek_str("[CDATA[") {
                        // CDATA sections only valid in foreign content (SVG/MathML)
                        // Treat as a bogus comment for HTML
                        self.advance_by(7);
                        self.parse_error("cdata-in-html-content");
                        self.comment.clear();
                        loop {
                            match self.current() {
                                None => break,
                                Some(']') if self.peek_str("]]>") => {
                                    self.advance_by(3);
                                    break;
                                }
                                Some(ch) => {
                                    self.comment.push(ch);
                                    self.advance();
                                }
                            }
                        }
                        self.state = State::Data;
                        return Some(self.emit_comment());
                    } else {
                        self.parse_error("incorrectly-opened-comment");
                        self.comment.clear();
                        // consume until >
                        while let Some(ch) = self.current() {
                            self.advance();
                            if ch == '>' { break; }
                            self.comment.push(ch);
                        }
                        self.state = State::Data;
                        return Some(self.emit_comment());
                    }
                }

                // ── Comment states ───────────────────────────────────────
                State::CommentStart => {
                    match c {
                        Some('-') => {
                            self.advance();
                            self.state = State::CommentStartDash;
                        }
                        Some('>') => {
                            self.parse_error("abrupt-closing-of-empty-comment");
                            self.advance();
                            self.state = State::Data;
                            return Some(self.emit_comment());
                        }
                        _ => {
                            self.state = State::Comment;
                        }
                    }
                }

                State::CommentStartDash => {
                    match c {
                        Some('-') => {
                            self.advance();
                            self.state = State::CommentEnd;
                        }
                        Some('>') => {
                            self.parse_error("abrupt-closing-of-empty-comment");
                            self.advance();
                            self.state = State::Data;
                            return Some(self.emit_comment());
                        }
                        None => {
                            self.parse_error("eof-in-comment");
                            self.state = State::Data;
                            return Some(self.emit_comment());
                        }
                        Some(_) => {
                            self.comment.push('-');
                            self.state = State::Comment;
                        }
                    }
                }

                State::Comment => {
                    match c {
                        Some('-') => {
                            self.advance();
                            self.state = State::CommentEndDash;
                        }
                        Some('\0') => {
                            self.parse_error("unexpected-null-character");
                            self.comment.push('\u{FFFD}');
                            self.advance();
                        }
                        None => {
                            self.parse_error("eof-in-comment");
                            self.state = State::Data;
                            return Some(self.emit_comment());
                        }
                        Some(ch) => {
                            self.comment.push(ch);
                            self.advance();
                        }
                    }
                }

                State::CommentEndDash => {
                    match c {
                        Some('-') => {
                            self.advance();
                            self.state = State::CommentEnd;
                        }
                        None => {
                            self.parse_error("eof-in-comment");
                            self.state = State::Data;
                            return Some(self.emit_comment());
                        }
                        Some(_) => {
                            self.comment.push('-');
                            self.state = State::Comment;
                        }
                    }
                }

                State::CommentEnd => {
                    match c {
                        Some('>') => {
                            self.advance();
                            self.state = State::Data;
                            return Some(self.emit_comment());
                        }
                        Some('!') => {
                            self.advance();
                            self.state = State::CommentEndBang;
                        }
                        Some('-') => {
                            self.comment.push('-');
                            self.advance();
                        }
                        None => {
                            self.parse_error("eof-in-comment");
                            self.state = State::Data;
                            return Some(self.emit_comment());
                        }
                        Some(_) => {
                            self.comment.push_str("--");
                            self.state = State::Comment;
                        }
                    }
                }

                State::CommentEndBang => {
                    match c {
                        Some('-') => {
                            self.comment.push_str("--!");
                            self.advance();
                            self.state = State::CommentEndDash;
                        }
                        Some('>') => {
                            self.parse_error("incorrectly-closed-comment");
                            self.advance();
                            self.state = State::Data;
                            return Some(self.emit_comment());
                        }
                        None => {
                            self.parse_error("eof-in-comment");
                            self.state = State::Data;
                            return Some(self.emit_comment());
                        }
                        Some(_) => {
                            self.comment.push_str("--!");
                            self.state = State::Comment;
                        }
                    }
                }

                // ── DOCTYPE states ───────────────────────────────────────
                State::Doctype => {
                    match c {
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => {
                            self.advance();
                            self.state = State::BeforeDoctypeName;
                        }
                        Some('>') => {
                            self.state = State::BeforeDoctypeName;
                            // reconsume
                        }
                        None => {
                            self.parse_error("eof-in-doctype");
                            self.doctype_force_quirks = true;
                            self.state = State::Data;
                            return Some(self.emit_doctype());
                        }
                        Some(_) => {
                            self.parse_error("missing-whitespace-before-doctype-name");
                            self.state = State::BeforeDoctypeName;
                            // reconsume
                        }
                    }
                }

                State::BeforeDoctypeName => {
                    match c {
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => {
                            self.advance();
                        }
                        Some('\0') => {
                            self.parse_error("unexpected-null-character");
                            self.doctype_name.push('\u{FFFD}');
                            self.advance();
                            self.state = State::DoctypeName;
                        }
                        Some('>') => {
                            self.parse_error("missing-doctype-name");
                            self.doctype_force_quirks = true;
                            self.advance();
                            self.state = State::Data;
                            return Some(self.emit_doctype());
                        }
                        None => {
                            self.parse_error("eof-in-doctype");
                            self.doctype_force_quirks = true;
                            self.state = State::Data;
                            return Some(self.emit_doctype());
                        }
                        Some(ch) => {
                            self.doctype_name.push(ch.to_ascii_lowercase());
                            self.advance();
                            self.state = State::DoctypeName;
                        }
                    }
                }

                State::DoctypeName => {
                    match c {
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => {
                            self.advance();
                            self.state = State::AfterDoctypeName;
                        }
                        Some('>') => {
                            self.advance();
                            self.state = State::Data;
                            return Some(self.emit_doctype());
                        }
                        Some('\0') => {
                            self.parse_error("unexpected-null-character");
                            self.doctype_name.push('\u{FFFD}');
                            self.advance();
                        }
                        None => {
                            self.parse_error("eof-in-doctype");
                            self.doctype_force_quirks = true;
                            self.state = State::Data;
                            return Some(self.emit_doctype());
                        }
                        Some(ch) => {
                            self.doctype_name.push(ch.to_ascii_lowercase());
                            self.advance();
                        }
                    }
                }

                State::AfterDoctypeName => {
                    match c {
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => {
                            self.advance();
                        }
                        Some('>') => {
                            self.advance();
                            self.state = State::Data;
                            return Some(self.emit_doctype());
                        }
                        None => {
                            self.parse_error("eof-in-doctype");
                            self.doctype_force_quirks = true;
                            self.state = State::Data;
                            return Some(self.emit_doctype());
                        }
                        Some(_) => {
                            if self.peek_str_ci("PUBLIC") {
                                self.advance_by(6);
                                self.state = State::AfterDoctypePublicKeyword;
                            } else if self.peek_str_ci("SYSTEM") {
                                self.advance_by(6);
                                self.state = State::AfterDoctypeSystemKeyword;
                            } else {
                                self.parse_error("invalid-character-sequence-after-doctype-name");
                                self.doctype_force_quirks = true;
                                // consume until >
                                while let Some(ch) = self.current() {
                                    self.advance();
                                    if ch == '>' { break; }
                                }
                                self.state = State::Data;
                                return Some(self.emit_doctype());
                            }
                        }
                    }
                }

                State::AfterDoctypePublicKeyword => {
                    match c {
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => {
                            self.advance();
                            self.state = State::BeforeDoctypePublicId;
                        }
                        Some('"') => {
                            self.parse_error("missing-whitespace-after-doctype-public-keyword");
                            self.doctype_public = Some(String::new());
                            self.advance();
                            self.state = State::DoctypePublicIdDoubleQuoted;
                        }
                        Some('\'') => {
                            self.parse_error("missing-whitespace-after-doctype-public-keyword");
                            self.doctype_public = Some(String::new());
                            self.advance();
                            self.state = State::DoctypePublicIdSingleQuoted;
                        }
                        Some('>') | None => {
                            self.doctype_force_quirks = true;
                            if c == Some('>') { self.advance(); }
                            self.state = State::Data;
                            return Some(self.emit_doctype());
                        }
                        Some(_) => {
                            self.doctype_force_quirks = true;
                            self.state = State::BogusDoctype;
                        }
                    }
                }

                State::BeforeDoctypePublicId => {
                    match c {
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => { self.advance(); }
                        Some('"') => {
                            self.doctype_public = Some(String::new());
                            self.advance();
                            self.state = State::DoctypePublicIdDoubleQuoted;
                        }
                        Some('\'') => {
                            self.doctype_public = Some(String::new());
                            self.advance();
                            self.state = State::DoctypePublicIdSingleQuoted;
                        }
                        _ => {
                            self.doctype_force_quirks = true;
                            self.state = State::BogusDoctype;
                        }
                    }
                }

                State::DoctypePublicIdDoubleQuoted => {
                    match c {
                        Some('"') => { self.advance(); self.state = State::AfterDoctypePublicId; }
                        Some('>') => {
                            self.doctype_force_quirks = true;
                            self.advance();
                            self.state = State::Data;
                            return Some(self.emit_doctype());
                        }
                        None => {
                            self.doctype_force_quirks = true;
                            return Some(self.emit_doctype());
                        }
                        Some(ch) => {
                            self.doctype_public.get_or_insert_default().push(ch);
                            self.advance();
                        }
                    }
                }

                State::DoctypePublicIdSingleQuoted => {
                    match c {
                        Some('\'') => { self.advance(); self.state = State::AfterDoctypePublicId; }
                        Some('>') => {
                            self.doctype_force_quirks = true;
                            self.advance();
                            self.state = State::Data;
                            return Some(self.emit_doctype());
                        }
                        None => {
                            self.doctype_force_quirks = true;
                            return Some(self.emit_doctype());
                        }
                        Some(ch) => {
                            self.doctype_public.get_or_insert_default().push(ch);
                            self.advance();
                        }
                    }
                }

                State::AfterDoctypePublicId => {
                    match c {
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => {
                            self.advance();
                            self.state = State::BetweenDoctypePublicAndSystemIds;
                        }
                        Some('>') => {
                            self.advance();
                            self.state = State::Data;
                            return Some(self.emit_doctype());
                        }
                        Some('"') => {
                            self.doctype_system = Some(String::new());
                            self.advance();
                            self.state = State::DoctypeSystemIdDoubleQuoted;
                        }
                        Some('\'') => {
                            self.doctype_system = Some(String::new());
                            self.advance();
                            self.state = State::DoctypeSystemIdSingleQuoted;
                        }
                        _ => {
                            self.doctype_force_quirks = true;
                            self.state = State::BogusDoctype;
                        }
                    }
                }

                State::BetweenDoctypePublicAndSystemIds => {
                    match c {
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => { self.advance(); }
                        Some('>') => {
                            self.advance();
                            self.state = State::Data;
                            return Some(self.emit_doctype());
                        }
                        Some('"') => {
                            self.doctype_system = Some(String::new());
                            self.advance();
                            self.state = State::DoctypeSystemIdDoubleQuoted;
                        }
                        Some('\'') => {
                            self.doctype_system = Some(String::new());
                            self.advance();
                            self.state = State::DoctypeSystemIdSingleQuoted;
                        }
                        _ => {
                            self.doctype_force_quirks = true;
                            self.state = State::BogusDoctype;
                        }
                    }
                }

                State::AfterDoctypeSystemKeyword => {
                    match c {
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => {
                            self.advance();
                            self.state = State::BeforeDoctypeSystemId;
                        }
                        Some('"') => {
                            self.doctype_system = Some(String::new());
                            self.advance();
                            self.state = State::DoctypeSystemIdDoubleQuoted;
                        }
                        Some('\'') => {
                            self.doctype_system = Some(String::new());
                            self.advance();
                            self.state = State::DoctypeSystemIdSingleQuoted;
                        }
                        _ => {
                            self.doctype_force_quirks = true;
                            self.state = State::BogusDoctype;
                        }
                    }
                }

                State::BeforeDoctypeSystemId => {
                    match c {
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => { self.advance(); }
                        Some('"') => {
                            self.doctype_system = Some(String::new());
                            self.advance();
                            self.state = State::DoctypeSystemIdDoubleQuoted;
                        }
                        Some('\'') => {
                            self.doctype_system = Some(String::new());
                            self.advance();
                            self.state = State::DoctypeSystemIdSingleQuoted;
                        }
                        _ => {
                            self.doctype_force_quirks = true;
                            self.state = State::BogusDoctype;
                        }
                    }
                }

                State::DoctypeSystemIdDoubleQuoted => {
                    match c {
                        Some('"') => { self.advance(); self.state = State::AfterDoctypeSystemId; }
                        Some('>') => {
                            self.doctype_force_quirks = true;
                            self.advance();
                            self.state = State::Data;
                            return Some(self.emit_doctype());
                        }
                        None => {
                            self.doctype_force_quirks = true;
                            return Some(self.emit_doctype());
                        }
                        Some(ch) => {
                            self.doctype_system.get_or_insert_default().push(ch);
                            self.advance();
                        }
                    }
                }

                State::DoctypeSystemIdSingleQuoted => {
                    match c {
                        Some('\'') => { self.advance(); self.state = State::AfterDoctypeSystemId; }
                        Some('>') => {
                            self.doctype_force_quirks = true;
                            self.advance();
                            self.state = State::Data;
                            return Some(self.emit_doctype());
                        }
                        None => {
                            self.doctype_force_quirks = true;
                            return Some(self.emit_doctype());
                        }
                        Some(ch) => {
                            self.doctype_system.get_or_insert_default().push(ch);
                            self.advance();
                        }
                    }
                }

                State::AfterDoctypeSystemId => {
                    match c {
                        Some('\t') | Some('\n') | Some('\x0C') | Some(' ') => { self.advance(); }
                        Some('>') => {
                            self.advance();
                            self.state = State::Data;
                            return Some(self.emit_doctype());
                        }
                        None => {
                            self.doctype_force_quirks = true;
                            return Some(self.emit_doctype());
                        }
                        Some(_) => {
                            self.parse_error("unexpected-character-after-doctype-system-identifier");
                            self.state = State::BogusDoctype;
                        }
                    }
                }

                State::BogusDoctype => {
                    match c {
                        Some('>') => {
                            self.advance();
                            self.state = State::Data;
                            return Some(self.emit_doctype());
                        }
                        None => {
                            self.state = State::Data;
                            return Some(self.emit_doctype());
                        }
                        Some(_) => { self.advance(); }
                    }
                }
            } // end match state
        } // end loop
    }

    // ------------------------------------------------------------------
    // Emit tag and configure raw-text state if needed
    // ------------------------------------------------------------------

    /// Emit the current tag. If it's a start tag for `script`, `style`,
    /// `textarea`, or `title`, also sets `raw_end_tag` for raw-text scanning.
    /// Returns the token; sets `self.state` side-effect-free (caller must
    /// call `next_state_after_tag`).
    fn emit_tag_and_set_raw_state(&mut self) -> Token {
        let is_start_raw = !self.tag.is_end
            && matches!(self.tag.name.as_str(), "script" | "style" | "textarea" | "title");
        if is_start_raw {
            self.raw_end_tag = self.tag.name.clone();
        }
        // Clear raw_end_tag on matching end tag so next_state_after_tag returns Data
        if self.tag.is_end && self.tag.name == self.raw_end_tag {
            self.raw_end_tag.clear();
        }
        self.emit_tag()
    }

    fn next_state_after_tag(&self) -> State {
        if self.raw_end_tag.is_empty() {
            return State::Data;
        }
        match self.raw_end_tag.as_str() {
            "script" | "style" => State::RawText,
            "textarea" | "title" => State::RCData,
            _ => State::Data,
        }
    }
}

// ---------------------------------------------------------------------------
// Named entity table (common HTML named entities)
// ---------------------------------------------------------------------------

/// Return (decoded_str, chars_consumed_including_semicolon) for a named
/// entity starting at the beginning of `s` (after `&`).
fn match_named_entity(s: &str) -> Option<(&'static str, usize)> {
    // We match the longest prefix to handle ambiguous cases.
    // Semicolon-terminated entities are preferred.
    const ENTITIES: &[(&str, &str)] = &[
        ("amp;", "&"),
        ("lt;", "<"),
        ("gt;", ">"),
        ("quot;", "\""),
        ("apos;", "'"),
        ("nbsp;", "\u{00A0}"),
        ("ndash;", "\u{2013}"),
        ("mdash;", "\u{2014}"),
        ("laquo;", "\u{00AB}"),
        ("raquo;", "\u{00BB}"),
        ("copy;", "\u{00A9}"),
        ("reg;", "\u{00AE}"),
        ("trade;", "\u{2122}"),
        ("euro;", "\u{20AC}"),
        ("pound;", "\u{00A3}"),
        ("yen;", "\u{00A5}"),
        ("cent;", "\u{00A2}"),
        ("deg;", "\u{00B0}"),
        ("plusmn;", "\u{00B1}"),
        ("times;", "\u{00D7}"),
        ("divide;", "\u{00F7}"),
        ("frac12;", "\u{00BD}"),
        ("frac14;", "\u{00BC}"),
        ("frac34;", "\u{00BE}"),
        ("hellip;", "\u{2026}"),
        ("lsquo;", "\u{2018}"),
        ("rsquo;", "\u{2019}"),
        ("ldquo;", "\u{201C}"),
        ("rdquo;", "\u{201D}"),
        ("bull;", "\u{2022}"),
        ("middot;", "\u{00B7}"),
        ("rarr;", "\u{2192}"),
        ("larr;", "\u{2190}"),
        ("uarr;", "\u{2191}"),
        ("darr;", "\u{2193}"),
        ("harr;", "\u{2194}"),
        ("infin;", "\u{221E}"),
        ("alpha;", "\u{03B1}"),
        ("beta;", "\u{03B2}"),
        ("gamma;", "\u{03B3}"),
        ("delta;", "\u{03B4}"),
        ("epsilon;", "\u{03B5}"),
        ("zeta;", "\u{03B6}"),
        ("eta;", "\u{03B7}"),
        ("theta;", "\u{03B8}"),
        ("iota;", "\u{03B9}"),
        ("kappa;", "\u{03BA}"),
        ("lambda;", "\u{03BB}"),
        ("mu;", "\u{03BC}"),
        ("nu;", "\u{03BD}"),
        ("xi;", "\u{03BE}"),
        ("pi;", "\u{03C0}"),
        ("rho;", "\u{03C1}"),
        ("sigma;", "\u{03C3}"),
        ("tau;", "\u{03C4}"),
        ("upsilon;", "\u{03C5}"),
        ("phi;", "\u{03C6}"),
        ("chi;", "\u{03C7}"),
        ("psi;", "\u{03C8}"),
        ("omega;", "\u{03C9}"),
        // Without trailing semicolon (legacy — only match if followed by
        // non-alphanumeric)
        ("amp", "&"),
        ("lt", "<"),
        ("gt", ">"),
        ("quot", "\""),
        ("apos", "'"),
        ("nbsp", "\u{00A0}"),
    ];

    // Try longest match first
    for &(name, decoded) in ENTITIES {
        if s.starts_with(name) {
            // For entities without `;`, ensure the next char is not alphanumeric
            if !name.ends_with(';') {
                let next = s[name.len()..].chars().next();
                match next {
                    Some(c) if c.is_ascii_alphanumeric() || c == '_' || c == '-' => continue,
                    _ => {}
                }
            }
            return Some((decoded, name.len()));
        }
    }
    None
}

// ---------------------------------------------------------------------------
// Convenience function
// ---------------------------------------------------------------------------

/// Tokenize an HTML string. Returns `(tokens, errors)`.
pub fn tokenize(html: &str) -> (Vec<Token>, Vec<String>) {
    Tokenizer::new(html).tokenize()
}
