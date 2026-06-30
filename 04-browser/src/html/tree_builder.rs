//! HTML5 tree builder — constructs the DOM from a token stream.
//!
//! Implements a practical subset of the HTML5 tree construction algorithm
//! (§13.2.6 of the spec), focusing on the cases that appear in real documents:
//!
//! * Insertion modes: Initial → BeforeHtml → BeforeHead → InHead →
//!   AfterHead → InBody → Text → AfterBody → AfterAfterBody
//! * Implied open elements (`<html>`, `<head>`, `<body>`)
//! * Implied end tags: `<p>` closed by block-level start tags;
//!   `<li>` closed by another `<li>`; `<dt>`/`<dd>` closed by each other
//! * Adoption agency algorithm (simplified): handles misnested formatting
//!   elements like `<b>`, `<i>`, `<a>`, `<em>`, `<strong>`
//! * Raw text / RCData elements (`<script>`, `<style>`, `<textarea>`, `<title>`)
//! * End-tag foster parenting: unknown end tags are discarded

use crate::html::dom::{
    classify_element, Attribute, Document, DoctypeData, ElementCategory, ElementData, NodeData,
    NodeId, DOCUMENT_NODE_ID,
};
use crate::html::tokenizer::{tokenize, Token};

// ---------------------------------------------------------------------------
// Insertion modes
// ---------------------------------------------------------------------------

/// The current position in the tree-construction state machine.
#[derive(Debug, Clone, PartialEq)]
enum InsertionMode {
    /// Before any `<html>` token.
    Initial,
    /// After `<!DOCTYPE>` or as default; expecting `<html>`.
    BeforeHtml,
    /// Saw `<html>`; expecting `<head>`.
    BeforeHead,
    /// Inside `<head>`.
    InHead,
    /// After `</head>`; expecting `<body>`.
    AfterHead,
    /// Inside `<body>` (the primary mode for most content).
    InBody,
    /// Inside a raw-text (`<script>`/`<style>`) or rcdata element.
    Text,
    /// After `</body>`.
    AfterBody,
    /// After `</html>`.
    AfterAfterBody,
}

// ---------------------------------------------------------------------------
// Tree builder
// ---------------------------------------------------------------------------

/// Builds a `Document` from HTML source.
pub struct TreeBuilder {
    doc: Document,
    /// Stack of open element ids (bottom = document root, top = current node).
    open: Vec<NodeId>,
    mode: InsertionMode,
    /// Head element id (saved so we can re-enter head if needed).
    head_ptr: Option<NodeId>,
    /// Whether we are inside a `<template>` element (simplified: just skip).
    in_template: bool,
    /// Frameset-ok flag (ignored for agent use; kept for spec accuracy).
    frameset_ok: bool,
}

impl TreeBuilder {
    fn new() -> Self {
        let doc = Document::new();
        TreeBuilder {
            doc,
            open: vec![DOCUMENT_NODE_ID],
            mode: InsertionMode::Initial,
            head_ptr: None,
            in_template: false,
            frameset_ok: true,
        }
    }

    // ------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------

    fn current_node(&self) -> NodeId {
        *self.open.last().expect("open elements stack is never empty")
    }

    fn insert_element(&mut self, tag: &str, attrs: Vec<Attribute>) -> NodeId {
        let (category, role) = classify_element(tag, &attrs);
        let data = NodeData::Element(ElementData {
            tag_name: tag.to_string(),
            attrs,
            category,
            role,
        });
        let id = self.doc.create_node(data);
        let parent = self.current_node();
        self.doc.append_child(parent, id);
        id
    }

    fn insert_and_push(&mut self, tag: &str, attrs: Vec<Attribute>) -> NodeId {
        let id = self.insert_element(tag, attrs);
        self.open.push(id);
        id
    }

    fn insert_text(&mut self, text: &str) {
        if text.is_empty() { return; }
        let parent = self.current_node();
        // Merge adjacent text nodes
        let last_child = self.doc.node(parent).children.last().copied();
        if let Some(last) = last_child {
            if let NodeData::Text(ref mut t) = self.doc.node_mut(last).data {
                t.push_str(text);
                return;
            }
        }
        let id = self.doc.create_node(NodeData::Text(text.to_string()));
        self.doc.append_child(parent, id);
    }

    fn insert_comment(&mut self, text: String) {
        let parent = self.current_node();
        let id = self.doc.create_node(NodeData::Comment(text));
        self.doc.append_child(parent, id);
    }

    /// Pop elements from the open stack until (and including) `tag`.
    /// Never pops the document root (index 0).
    fn pop_until(&mut self, tag: &str) {
        while self.open.len() > 1 {
            let top = *self.open.last().unwrap();
            let name = self.doc.node(top).tag_name().map(|s| s.to_string());
            self.open.pop();
            if name.as_deref() == Some(tag) { break; }
        }
    }

    /// Pop elements from the open stack until (and including) the first
    /// element matching any name in `tags`. Never pops the document root.
    fn pop_until_any(&mut self, tags: &[&str]) {
        while self.open.len() > 1 {
            let top = *self.open.last().unwrap();
            let name = self.doc.node(top).tag_name().map(|s| s.to_string());
            self.open.pop();
            if name.as_deref().map(|n| tags.contains(&n)).unwrap_or(false) {
                break;
            }
        }
    }

    /// Pop the top of the open stack (current node).
    fn pop(&mut self) -> Option<NodeId> {
        if self.open.len() > 1 { self.open.pop() } else { None }
    }

    /// Check whether `tag` is in the open-elements stack.
    fn in_open_stack(&self, tag: &str) -> bool {
        self.open.iter().rev().any(|&id| {
            self.doc.node(id).tag_name() == Some(tag)
        })
    }

    /// Check whether `tag` is in scope (simplified: scan open stack stopping
    /// at scope boundaries: html, table, td, th, caption, template).
    fn in_scope(&self, tag: &str) -> bool {
        const SCOPE_BOUNDARY: &[&str] = &[
            "html", "table", "td", "th", "caption", "template",
            "applet", "marquee", "object",
        ];
        for &id in self.open.iter().rev() {
            let name = match self.doc.node(id).tag_name() {
                Some(n) => n,
                None => continue,
            };
            if name == tag { return true; }
            if SCOPE_BOUNDARY.contains(&name) { return false; }
        }
        false
    }

    /// Generate implied end tags, optionally excluding `except`.
    ///
    /// Implied end tags are `dd dt li optgroup option p rb rp rt rtc`.
    fn generate_implied_end_tags(&mut self, except: Option<&str>) {
        const IMPLIED: &[&str] = &[
            "dd", "dt", "li", "optgroup", "option", "p", "rb", "rp", "rt", "rtc",
        ];
        loop {
            let top = self.current_node();
            let name = match self.doc.node(top).tag_name() {
                Some(n) => n.to_string(),
                None => break,
            };
            if except == Some(name.as_str()) { break; }
            if IMPLIED.contains(&name.as_str()) {
                self.open.pop();
            } else {
                break;
            }
        }
    }

    /// Close a `<p>` element if one is open in button scope.
    fn close_p_if_open(&mut self) {
        const P_SCOPE_BOUNDARY: &[&str] = &[
            "html", "table", "td", "th", "caption", "template",
            "applet", "marquee", "object", "button",
        ];
        let p_in_scope = self.open.iter().rev().any(|&id| {
            let name = self.doc.node(id).tag_name().unwrap_or("");
            if name == "p" { return true; }
            if P_SCOPE_BOUNDARY.contains(&name) { return false; }
            false
        });
        if p_in_scope {
            self.generate_implied_end_tags(Some("p"));
            self.pop_until("p");
        }
    }

    /// Reconstruct the active formatting elements (simplified adoption agency).
    ///
    /// The full adoption agency algorithm (spec §13.2.8.3) handles deeply
    /// misnested formatting elements. Our simplified version covers the common
    /// case: when we see an end tag for a formatting element that is not the
    /// current node, we walk the stack to find it and pop everything up to it.
    fn adoption_agency(&mut self, tag: &str) {
        // If it's on the stack, pop to it
        if self.in_open_stack(tag) {
            self.generate_implied_end_tags(Some(tag));
            self.pop_until(tag);
        }
        // Otherwise ignore (parse error already logged by caller)
    }

    // ------------------------------------------------------------------
    // Ensure basic structure exists
    // ------------------------------------------------------------------

    fn ensure_html(&mut self) -> NodeId {
        if let Some(id) = self.doc.find_element("html") {
            return id;
        }
        let id = self.insert_and_push("html", vec![]);
        self.mode = InsertionMode::BeforeHead;
        id
    }

    fn ensure_head(&mut self) -> NodeId {
        if let Some(id) = self.head_ptr { return id; }
        let id = self.insert_and_push("head", vec![]);
        self.head_ptr = Some(id);
        id
    }

    fn ensure_body(&mut self) -> NodeId {
        if let Some(id) = self.doc.find_element("body") {
            // Make body current if it's not already
            if !self.open.contains(&id) {
                self.open.push(id);
            }
            return id;
        }
        // Close head if open
        if let Some(hid) = self.head_ptr {
            if self.open.last() == Some(&hid) {
                self.open.pop();
            }
        }
        let id = self.insert_and_push("body", vec![]);
        self.mode = InsertionMode::InBody;
        id
    }

    // ------------------------------------------------------------------
    // Token processing
    // ------------------------------------------------------------------

    fn process(&mut self, token: Token) {
        match self.mode {
            InsertionMode::Initial => self.process_initial(token),
            InsertionMode::BeforeHtml => self.process_before_html(token),
            InsertionMode::BeforeHead => self.process_before_head(token),
            InsertionMode::InHead => self.process_in_head(token),
            InsertionMode::AfterHead => self.process_after_head(token),
            InsertionMode::InBody => self.process_in_body(token),
            InsertionMode::Text => self.process_text(token),
            InsertionMode::AfterBody => self.process_after_body(token),
            InsertionMode::AfterAfterBody => self.process_after_after_body(token),
        }
    }

    // ── Initial ──────────────────────────────────────────────────────────

    fn process_initial(&mut self, token: Token) {
        match token {
            Token::Text(ref t) if t.trim().is_empty() => { /* ignore whitespace */ }
            Token::Comment(c) => {
                self.insert_comment(c);
            }
            Token::Doctype { name, public_id, system_id, force_quirks } => {
                let id = self.doc.create_node(NodeData::Doctype(DoctypeData {
                    name,
                    public_id: public_id.unwrap_or_default(),
                    system_id: system_id.unwrap_or_default(),
                    force_quirks,
                }));
                self.doc.append_child(DOCUMENT_NODE_ID, id);
                self.mode = InsertionMode::BeforeHtml;
            }
            _ => {
                // Anything else — treat as if we're in BeforeHtml
                self.mode = InsertionMode::BeforeHtml;
                self.process(token);
            }
        }
    }

    // ── BeforeHtml ────────────────────────────────────────────────────────

    fn process_before_html(&mut self, token: Token) {
        match token {
            Token::Text(ref t) if t.trim().is_empty() => { /* ignore */ }
            Token::Comment(c) => { self.insert_comment(c); }
            Token::StartTag { ref name, ref attrs, .. } if name == "html" => {
                let _id = self.insert_and_push("html", attrs.clone());
                self.mode = InsertionMode::BeforeHead;
            }
            Token::EndTag { ref name } if !matches!(name.as_str(), "head" | "body" | "html" | "br") => {
                // Parse error — ignore
                self.doc.errors.push(format!("unexpected end tag </{name}> before <html>"));
            }
            Token::Eof => {
                // Implied <html> — don't push it (nothing after)
            }
            _ => {
                // Anything else: create an implied <html> and reprocess
                self.ensure_html();
                self.mode = InsertionMode::BeforeHead;
                self.process(token);
            }
        }
    }

    // ── BeforeHead ────────────────────────────────────────────────────────

    fn process_before_head(&mut self, token: Token) {
        match token {
            Token::Text(ref t) if t.trim().is_empty() => { /* ignore */ }
            Token::Comment(c) => { self.insert_comment(c); }
            Token::StartTag { ref name, .. } if name == "html" => {
                self.process_in_body(token); // spec: process as in body
            }
            Token::StartTag { ref name, ref attrs, .. } if name == "head" => {
                let id = self.insert_and_push("head", attrs.clone());
                self.head_ptr = Some(id);
                self.mode = InsertionMode::InHead;
            }
            Token::EndTag { ref name }
                if !matches!(name.as_str(), "head" | "body" | "html" | "br") =>
            {
                self.doc.errors.push(format!("unexpected end tag </{name}> before head"));
            }
            Token::Eof => {}
            _ => {
                // Implied <head>
                let id = self.insert_and_push("head", vec![]);
                self.head_ptr = Some(id);
                self.mode = InsertionMode::InHead;
                self.process(token);
            }
        }
    }

    // ── InHead ────────────────────────────────────────────────────────────

    fn process_in_head(&mut self, token: Token) {
        match token {
            Token::Text(ref t) if t.trim().is_empty() => {
                self.insert_text(t);
            }
            Token::Comment(c) => { self.insert_comment(c); }
            Token::StartTag { ref name, .. } if name == "html" => {
                self.process_in_body(token);
            }
            Token::StartTag { ref name, ref attrs, .. }
                if matches!(name.as_str(), "base" | "basefont" | "bgsound" | "link" | "meta") =>
            {
                // Void elements in head — insert and don't push
                self.insert_element(name, attrs.clone());
            }
            Token::StartTag { ref name, ref attrs, .. } if name == "title" => {
                self.insert_and_push("title", attrs.clone());
                self.mode = InsertionMode::Text;
            }
            Token::StartTag { ref name, ref attrs, .. }
                if matches!(name.as_str(), "noframes" | "style") =>
            {
                self.insert_and_push(name, attrs.clone());
                self.mode = InsertionMode::Text;
            }
            Token::StartTag { ref name, ref attrs, .. } if name == "noscript" => {
                self.insert_and_push("noscript", attrs.clone());
                // simplified: treat contents as text (no scripting)
            }
            Token::StartTag { ref name, ref attrs, .. } if name == "script" => {
                self.insert_and_push("script", attrs.clone());
                self.mode = InsertionMode::Text;
            }
            Token::EndTag { ref name } if name == "head" => {
                self.pop(); // pop <head>
                self.mode = InsertionMode::AfterHead;
            }
            Token::EndTag { ref name }
                if matches!(name.as_str(), "body" | "html" | "br") =>
            {
                // Implied </head> then reprocess
                self.pop(); // pop head
                self.mode = InsertionMode::AfterHead;
                self.process(token);
            }
            Token::StartTag { ref name, ref attrs, .. } if name == "template" => {
                self.insert_and_push("template", attrs.clone());
                self.in_template = true;
            }
            Token::EndTag { ref name } if name == "template" => {
                if self.in_open_stack("template") {
                    self.pop_until("template");
                }
                self.in_template = false;
            }
            Token::EndTag { ref name } if name != "head" => {
                self.doc.errors.push(format!("unexpected end tag </{name}> in head"));
            }
            Token::Eof => {
                self.pop(); // pop head
                self.mode = InsertionMode::AfterHead;
                self.process(token);
            }
            _ => {
                // Anything else: implied </head>, reprocess
                self.pop(); // pop head
                self.mode = InsertionMode::AfterHead;
                self.process(token);
            }
        }
    }

    // ── AfterHead ─────────────────────────────────────────────────────────

    fn process_after_head(&mut self, token: Token) {
        match token {
            Token::Text(ref t) if t.trim().is_empty() => {
                self.insert_text(t);
            }
            Token::Comment(c) => { self.insert_comment(c); }
            Token::StartTag { ref name, .. } if name == "html" => {
                self.process_in_body(token);
            }
            Token::StartTag { ref name, ref attrs, .. } if name == "body" => {
                self.insert_and_push("body", attrs.clone());
                self.frameset_ok = false;
                self.mode = InsertionMode::InBody;
            }
            Token::StartTag { ref name, ref attrs, .. } if name == "frameset" => {
                self.insert_and_push("frameset", attrs.clone());
                self.mode = InsertionMode::InBody; // simplified
            }
            Token::StartTag { ref name, ref attrs, .. }
                if matches!(name.as_str(),
                    "base" | "basefont" | "bgsound" | "link" | "meta"
                    | "noframes" | "script" | "style" | "template" | "title") =>
            {
                // These should have been in head — insert into head if available
                self.doc.errors.push(format!("misplaced head element <{name}> after head"));
                if let Some(head_id) = self.head_ptr {
                    self.open.push(head_id);
                    self.process_in_head(token);
                    self.open.retain(|&id| id != head_id);
                }
            }
            Token::EndTag { ref name } if name == "template" => {
                self.process_in_head(token);
            }
            Token::EndTag { ref name }
                if !matches!(name.as_str(), "body" | "html" | "br") =>
            {
                self.doc.errors.push(format!("unexpected end tag </{name}> after head"));
            }
            Token::Eof => { /* nothing */ }
            _ => {
                // Implied <body>
                self.insert_and_push("body", vec![]);
                self.mode = InsertionMode::InBody;
                self.process(token);
            }
        }
    }

    // ── InBody ────────────────────────────────────────────────────────────

    fn process_in_body(&mut self, token: Token) {
        match token {
            Token::Text(ref t) if t == "\0" => {
                self.doc.errors.push("unexpected-null-character in body".into());
            }
            Token::Text(t) => {
                self.insert_text(&t);
                self.frameset_ok = false;
            }
            Token::Comment(c) => { self.insert_comment(c); }
            Token::Doctype { .. } => {
                self.doc.errors.push("unexpected DOCTYPE in body".into());
            }

            // ── Start tags ──────────────────────────────────────────────
            Token::StartTag { ref name, ref attrs, .. } if name == "html" => {
                self.doc.errors.push("unexpected <html> in body".into());
                // Merge attributes into existing html element (simplified: ignore)
            }
            Token::StartTag { ref name, ref attrs, .. }
                if matches!(name.as_str(),
                    "base" | "basefont" | "bgsound" | "link" | "meta" | "noframes"
                    | "script" | "style" | "template" | "title") =>
            {
                self.process_in_head(token);
            }
            Token::StartTag { ref name, ref attrs, .. } if name == "body" => {
                self.doc.errors.push("unexpected <body> in body".into());
                // Merge attributes — simplified: ignore
            }
            Token::StartTag { ref name, ref attrs, .. } if name == "frameset" => {
                self.doc.errors.push("unexpected <frameset> in body".into());
            }

            // Block elements that close an open <p>
            Token::StartTag { ref name, ref attrs, .. }
                if is_p_closing_start_tag(name) =>
            {
                self.close_p_if_open();
                self.insert_and_push(name, attrs.clone());
            }

            // Heading elements close other headings and open <p>
            Token::StartTag { ref name, ref attrs, .. }
                if matches!(name.as_str(), "h1" | "h2" | "h3" | "h4" | "h5" | "h6") =>
            {
                self.close_p_if_open();
                // Close any open heading
                let top = self.current_node();
                if let Some(tag) = self.doc.node(top).tag_name() {
                    if matches!(tag, "h1" | "h2" | "h3" | "h4" | "h5" | "h6") {
                        self.doc.errors.push(format!("heading inside heading: <{name}>"));
                        self.pop();
                    }
                }
                self.insert_and_push(name, attrs.clone());
            }

            // pre, listing: close p, set frameset-ok = false
            Token::StartTag { ref name, ref attrs, .. }
                if matches!(name.as_str(), "pre" | "listing") =>
            {
                self.close_p_if_open();
                self.insert_and_push(name, attrs.clone());
                self.frameset_ok = false;
                // Skip a leading newline in next token (handled by merging)
            }

            // form: only one active form at a time (simplified)
            Token::StartTag { ref name, ref attrs, .. } if name == "form" => {
                self.close_p_if_open();
                self.insert_and_push("form", attrs.clone());
            }

            // li: close any open li
            Token::StartTag { ref name, ref attrs, .. } if name == "li" => {
                self.frameset_ok = false;
                // Close any open li on the stack
                if self.in_open_stack("li") {
                    self.generate_implied_end_tags(Some("li"));
                    if self.doc.node(self.current_node()).tag_name() != Some("li") {
                        self.doc.errors.push("unexpected li inside other list item".into());
                    }
                    self.pop_until("li");
                }
                self.close_p_if_open();
                self.insert_and_push("li", attrs.clone());
            }

            // dt/dd: close any open dt or dd
            Token::StartTag { ref name, ref attrs, .. }
                if matches!(name.as_str(), "dt" | "dd") =>
            {
                self.frameset_ok = false;
                if self.in_open_stack("dt") || self.in_open_stack("dd") {
                    self.generate_implied_end_tags(Some(name));
                    self.pop_until_any(&["dt", "dd"]);
                }
                self.close_p_if_open();
                self.insert_and_push(name, attrs.clone());
            }

            // Void elements and formatting elements that don't push
            Token::StartTag { ref name, ref attrs, self_closing } => {
                let (cat, _) = classify_element(name, attrs);
                match cat {
                    ElementCategory::Void => {
                        // Don't push void elements onto the stack
                        self.insert_element(name, attrs.clone());
                    }
                    ElementCategory::RawText => {
                        // script, style
                        self.insert_and_push(name, attrs.clone());
                        self.mode = InsertionMode::Text;
                    }
                    ElementCategory::EscapableRawText => {
                        // textarea, title
                        self.insert_and_push(name, attrs.clone());
                        self.frameset_ok = false;
                        self.mode = InsertionMode::Text;
                    }
                    _ => {
                        // Normal element (or self_closing — treat as start+end)
                        self.insert_and_push(name, attrs.clone());
                        if self_closing {
                            self.pop();
                        }
                    }
                }
            }

            // ── End tags ─────────────────────────────────────────────────
            Token::EndTag { ref name } if name == "body" => {
                if !self.in_scope("body") {
                    self.doc.errors.push("</body> with no body in scope".into());
                    return;
                }
                self.mode = InsertionMode::AfterBody;
            }
            Token::EndTag { ref name } if name == "html" => {
                if !self.in_scope("body") {
                    self.doc.errors.push("</html> with no body in scope".into());
                    return;
                }
                self.mode = InsertionMode::AfterBody;
                self.process(token); // reprocess as AfterBody
            }

            // Block elements that close an open <p>
            Token::EndTag { ref name }
                if is_p_closing_end_tag(name) =>
            {
                if !self.in_scope(name) {
                    self.doc.errors.push(format!("</{name}> not in scope"));
                    // Spec says: if it's a p tag, insert a p and immediately close it
                    if name == "p" {
                        let _id = self.insert_element("p", vec![]);
                        return;
                    }
                    return;
                }
                self.generate_implied_end_tags(Some(name));
                if self.doc.node(self.current_node()).tag_name() != Some(name.as_str()) {
                    self.doc.errors.push(format!("misnested end tag </{name}>"));
                }
                self.pop_until(name);
            }

            // li
            Token::EndTag { ref name } if name == "li" => {
                if !self.in_scope("li") {
                    self.doc.errors.push("</li> not in scope".into());
                    return;
                }
                self.generate_implied_end_tags(Some("li"));
                self.pop_until("li");
            }

            // dt/dd
            Token::EndTag { ref name } if matches!(name.as_str(), "dt" | "dd") => {
                if !self.in_scope(name) {
                    self.doc.errors.push(format!("</{name}> not in scope"));
                    return;
                }
                self.generate_implied_end_tags(Some(name));
                self.pop_until(name);
            }

            // Headings
            Token::EndTag { ref name }
                if matches!(name.as_str(), "h1" | "h2" | "h3" | "h4" | "h5" | "h6") =>
            {
                let in_scope = ["h1", "h2", "h3", "h4", "h5", "h6"]
                    .iter().any(|h| self.in_scope(h));
                if !in_scope {
                    self.doc.errors.push(format!("</{name}> not in scope"));
                    return;
                }
                self.generate_implied_end_tags(Some(name));
                self.pop_until_any(&["h1", "h2", "h3", "h4", "h5", "h6"]);
            }

            // Formatting element end tags: simplified adoption agency
            Token::EndTag { ref name }
                if matches!(name.as_str(),
                    "a" | "b" | "big" | "code" | "em" | "font" | "i" | "nobr"
                    | "s" | "small" | "strike" | "strong" | "tt" | "u") =>
            {
                self.adoption_agency(name);
            }

            // Generic end tags
            Token::EndTag { ref name } => {
                // Pop up to (and including) the matching open element
                if self.in_open_stack(name) {
                    self.generate_implied_end_tags(Some(name));
                    self.pop_until(name);
                } else {
                    self.doc.errors.push(format!("end tag </{name}> with no matching open element"));
                }
            }

            Token::Eof => {
                // Stop — any open elements stay unclosed (they get implicit close)
            }
        }
    }

    // ── Text ──────────────────────────────────────────────────────────────

    fn process_text(&mut self, token: Token) {
        match token {
            Token::Text(t) => { self.insert_text(&t); }
            Token::Eof => {
                self.doc.errors.push("EOF in text/raw-text element".into());
                self.pop();
                self.mode = InsertionMode::InBody;
            }
            Token::EndTag { .. } => {
                self.pop();
                // Return to InBody (or AfterHead for title/style in head)
                if self.doc.find_element("body").is_some()
                    && self.open.iter().any(|&id| self.doc.node(id).tag_name() == Some("body"))
                {
                    self.mode = InsertionMode::InBody;
                } else {
                    self.mode = InsertionMode::AfterHead;
                }
            }
            _ => {
                // Parse error: non-text token in text mode (shouldn't happen)
                self.pop();
                self.mode = InsertionMode::InBody;
                self.process(token);
            }
        }
    }

    // ── AfterBody ─────────────────────────────────────────────────────────

    fn process_after_body(&mut self, token: Token) {
        match token {
            Token::Text(ref t) if t.trim().is_empty() => {
                self.process_in_body(token);
            }
            Token::Comment(c) => {
                // Comment after body goes after the html element
                let html_id = self.doc.find_element("html").unwrap_or(DOCUMENT_NODE_ID);
                let id = self.doc.create_node(NodeData::Comment(c));
                self.doc.append_child(html_id, id);
            }
            Token::StartTag { ref name, .. } if name == "html" => {
                self.process_in_body(token);
            }
            Token::EndTag { ref name } if name == "html" => {
                self.mode = InsertionMode::AfterAfterBody;
            }
            Token::Eof => { /* done */ }
            _ => {
                self.doc.errors.push("unexpected token after body".into());
                self.mode = InsertionMode::InBody;
                self.process(token);
            }
        }
    }

    // ── AfterAfterBody ────────────────────────────────────────────────────

    fn process_after_after_body(&mut self, token: Token) {
        match token {
            Token::Comment(c) => {
                let id = self.doc.create_node(NodeData::Comment(c));
                self.doc.append_child(DOCUMENT_NODE_ID, id);
            }
            Token::Text(ref t) if t.trim().is_empty() => {
                self.process_in_body(token);
            }
            Token::StartTag { ref name, .. } if name == "html" => {
                self.process_in_body(token);
            }
            Token::Eof => { /* done */ }
            _ => {
                self.doc.errors.push("unexpected token after </html>".into());
                self.mode = InsertionMode::InBody;
                self.process(token);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Helpers — which start/end tags trigger implied p-closing
// ---------------------------------------------------------------------------

/// Start tags that close an open `<p>` element.
fn is_p_closing_start_tag(name: &str) -> bool {
    matches!(name,
        "address" | "article" | "aside" | "blockquote" | "center" | "details"
        | "dialog" | "dir" | "div" | "dl" | "fieldset" | "figcaption" | "figure"
        | "footer" | "header" | "hgroup" | "hr" | "main" | "menu" | "nav"
        | "ol" | "p" | "section" | "summary" | "table" | "ul"
        | "h1" | "h2" | "h3" | "h4" | "h5" | "h6"
        | "pre" | "listing" | "form"
    )
}

/// End tags that should close an open `<p>` first (block-level closing tags).
fn is_p_closing_end_tag(name: &str) -> bool {
    matches!(name,
        "address" | "article" | "aside" | "blockquote" | "button" | "center"
        | "details" | "dialog" | "dir" | "div" | "dl" | "fieldset" | "figcaption"
        | "figure" | "footer" | "header" | "hgroup" | "listing" | "main" | "menu"
        | "nav" | "ol" | "pre" | "section" | "summary" | "table" | "ul" | "p"
    )
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// Parse an HTML string into a `Document`.
///
/// This is the main entry point for the HTML parser. It tokenizes the input
/// and then builds the DOM tree, recovering from errors along the way.
pub fn parse(html: &str) -> Document {
    let (tokens, tok_errors) = tokenize(html);
    let mut builder = TreeBuilder::new();

    // Record tokenizer errors
    for e in tok_errors {
        builder.doc.errors.push(e);
    }

    // Ensure we start with an implied <html> frame if there's no explicit one
    // (handled lazily in process_before_html/process_initial)

    for token in tokens {
        builder.process(token);
    }

    // Close any remaining open elements (implicit EOF handling)
    // This is a no-op for well-formed HTML, but ensures the tree is valid
    // for broken HTML (all open elements get their text content kept).

    builder.doc
}
