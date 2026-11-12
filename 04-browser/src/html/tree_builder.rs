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
//! * Adoption agency algorithm (partial): misnested formatting elements like
//!   `<b>`, `<i>`, `<a>`, `<em>`, `<strong>` are re-opened after the stray end
//!   tag closes, per §13.2.8.3
//! * Raw text / RCData elements (`<script>`, `<style>`, `<textarea>`, `<title>`)
//! * End-tag foster parenting: unknown end tags are discarded

use crate::html::dom::{
    classify_element, Attribute, DoctypeData, Document, ElementCategory, ElementData, NodeData,
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

/// An entry in the list of active formatting elements (HTML Living Standard
/// §13.2.6).
///
/// The list holds the formatting elements that are currently open, plus
/// *markers*. A marker bounds the search for an element with the same tag
/// name: the search never reaches past the last marker, so formatting that
/// has been separated by a non-formatting element is out of scope.
#[derive(Debug, Clone)]
struct AfeEntry {
    /// The element's tag name.
    name: String,
    /// True for a marker entry.
    marker: bool,
    /// The element this entry refers to (`None` for markers). The Noah's
    /// Ark clause compares tag name, attributes and TEXT, which needs the
    /// element itself, not just its name.
    id: Option<NodeId>,
}

/// The outcome of the adoption agency's OUTER loop (§13.2.6.4).
///
/// The outer loop is the part of the algorithm that decides *which*
/// element it is going to work on, and what to do when the list and the
/// stack of open elements disagree. The inner loop — the part that moves
/// nodes around — is separate.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum OuterOutcome {
    /// The inner step ran to completion.
    InnerCompleted,
    /// Parse error: there is no formatting element with that tag name
    /// between the end of the list and the last marker (§13.2.6.4 step 7).
    /// The spec says to act as described in the "any other end tag" entry.
    NoSuchElement,
    /// Parse error: the element is not in the stack of open elements
    /// (§13.2.6.4 step 8). The entry is removed from the list and the
    /// algorithm aborts.
    NotInOpenStack,
    /// Parse error: the element is in the stack of open elements but is
    /// not in scope (§13.2.6.4 step 9). The algorithm aborts.
    NotInScope,
    /// The eight-iteration cap was reached (§13.2.6.4 step 5).
    IterationLimit,
}

/// Builds a `Document` from HTML source.
pub struct TreeBuilder {
    doc: Document,
    /// Stack of open element ids (bottom = document root, top = current node).
    open: Vec<NodeId>,
    /// The list of active formatting elements (§13.2.6).
    afe: Vec<AfeEntry>,
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
            afe: Vec::new(),
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
        *self
            .open
            .last()
            .expect("open elements stack is never empty")
    }

    fn insert_element(&mut self, tag: &str, attrs: Vec<Attribute>) -> NodeId {
        // §13.2.6: some elements clear the list of active formatting
        // elements back to the last marker when they are inserted.
        if Self::clears_list(tag) {
            self.clear_afe_to_last_marker();
        }
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
        // §13.2.6: some elements push a marker onto the list of active
        // formatting elements when they are inserted.
        if Self::pushes_marker(tag) {
            self.push_afe_marker();
        }
        id
    }

    /// §13.2.6: the elements that push a marker onto the list of active
    /// formatting elements when they are inserted.
    ///
    /// The spec's in-body rules name four tags: applet, marquee, object
    /// and template. html5lib 1.1's source agrees for the first three --
    /// `startTagAppletMarqueeObject` is its only marker site in the body
    /// mode -- and has no template rule at all (the spec's is newer).
    ///
    /// It does NOT push a marker for `button`, and it does not push one
    /// for a `p` start tag. Slice 6 pushed both, from a reading of the
    /// spec that was wrong; html5lib's source is the check that caught
    /// it.
    ///
    /// The spec also inserts markers for caption, td and th -- but in the
    /// table, caption and cell insertion modes, which this component does
    /// not model (it has no table modes at all), so those rules are
    /// deliberately out of scope here rather than applied generically.
    ///
    /// Everything else leaves the list alone -- and that is the whole
    /// point of a marker: it marks the boundary where formatting stops
    /// being in scope. Most elements do NOT end formatting's scope, which
    /// is why a marker before every element (what this component used to
    /// do) throws away the scope it was meant to mark.
    fn pushes_marker(tag: &str) -> bool {
        matches!(tag, "applet" | "marquee" | "object" | "template")
    }

    /// §13.2.6: the elements that clear the list of active formatting
    /// elements back to the last marker when they are inserted.
    fn clears_list(tag: &str) -> bool {
        matches!(
            tag,
            "caption" | "col" | "colgroup" | "frame" | "head" | "tbody" | "td" | "tfoot" | "th"
                | "thead" | "tr"
        )
    }

    /// §13.2.6: clear the list of active formatting elements back to the
    /// last marker -- remove everything from the end of the list, up to
    /// and including the last marker itself.
    fn clear_afe_to_last_marker(&mut self) {
        match self.afe.iter().rposition(|e| e.marker) {
            Some(pos) => self.afe.truncate(pos),
            None => self.afe.clear(),
        }
    }

    fn insert_and_push(&mut self, tag: &str, attrs: Vec<Attribute>) -> NodeId {
        let id = self.insert_element(tag, attrs);
        self.open.push(id);
        if Self::is_formatting_element(tag) {
            self.afe.push(AfeEntry {
                name: tag.to_string(),
                marker: false,
                id: Some(id),
            });
            // §13.2.6: after pushing, if the list now holds three elements
            // with the same tag name, namespace, attributes and text, the
            // earliest of them is removed.
            self.noahs_ark(tag, id);
        }
        id
    }

    /// The Noah's Ark clause (§13.2.6): if the list of active formatting
    /// elements contains three elements with the same tag name, namespace,
    /// attributes and text, remove the earliest one.
    ///
    /// The clause keeps the list from growing without bound when a
    /// document repeats the same formatting element. It compares TEXT as
    /// well as name and attributes, and the text of an element changes as
    /// the parser fills it, so the clause only fires for elements that
    /// are still empty: `<b><b><b><b>` collapses the first `<b>`, while
    /// `<b>1<b>2<b>3<b>4` does not, because by the time the fourth `<b>`
    /// is inserted the first three hold different text.
    fn noahs_ark(&mut self, tag: &str, id: NodeId) {
        let mut identical: Vec<usize> = Vec::new();
        for i in 0..self.afe.len() {
            let entry = &self.afe[i];
            if entry.marker || entry.name != tag {
                continue;
            }
            let Some(other) = entry.id else { continue; };
            if self.same_formatting(other, id) {
                identical.push(i);
            }
        }
        if identical.len() >= 3 {
            // Remove the EARLIEST of the identical entries.
            let earliest = identical[0];
            self.afe.remove(earliest);
        }
    }

    /// Same tag name, attributes and text — the comparison the Noah's Ark
    /// clause makes (§13.2.6). Namespaces are not modelled in this
    /// component, so the comparison is name (checked by the caller),
    /// attributes and text.
    fn same_formatting(&self, a: NodeId, b: NodeId) -> bool {
        let attrs_equal = match (
            self.doc.node(a).element_data().map(|e| &e.attrs),
            self.doc.node(b).element_data().map(|e| &e.attrs),
        ) {
            (Some(x), Some(y)) => x == y,
            (None, None) => true,
            _ => false,
        };
        attrs_equal && self.doc.text_content(a) == self.doc.text_content(b)
    }

    fn insert_text(&mut self, text: &str) {
        if text.is_empty() {
            return;
        }
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
            if let Some(ref n) = name {
                // §13.2.6: an element that is popped off the stack of open
                // elements is removed from the list of active formatting
                // elements.
                self.remove_afe(n);
            }
            if name.as_deref() == Some(tag) {
                break;
            }
        }
    }

    /// Pop elements from the open stack until (and including) the first
    /// element matching any name in `tags`. Never pops the document root.
    fn pop_until_any(&mut self, tags: &[&str]) {
        while self.open.len() > 1 {
            let top = *self.open.last().unwrap();
            let name = self.doc.node(top).tag_name().map(|s| s.to_string());
            self.open.pop();
            if let Some(ref n) = name {
                self.remove_afe(n);
            }
            if name.as_deref().map(|n| tags.contains(&n)).unwrap_or(false) {
                break;
            }
        }
    }

    /// Pop the top of the open stack (current node).
    fn pop(&mut self) -> Option<NodeId> {
        if self.open.len() > 1 {
            let popped = self.open.pop();
            if let Some(id) = popped {
                let name = self.doc.node(id).tag_name().map(|s| s.to_string());
                if let Some(n) = name {
                    self.remove_afe(&n);
                }
            }
            popped
        } else {
            None
        }
    }

    /// Check whether `tag` is in the open-elements stack.
    fn in_open_stack(&self, tag: &str) -> bool {
        self.open
            .iter()
            .rev()
            .any(|&id| self.doc.node(id).tag_name() == Some(tag))
    }

    /// Check whether `tag` is in scope (simplified: scan open stack stopping
    /// at scope boundaries: html, table, td, th, caption, template).
    fn in_scope(&self, tag: &str) -> bool {
        const SCOPE_BOUNDARY: &[&str] = &[
            "html", "table", "td", "th", "caption", "template", "applet", "marquee", "object",
        ];
        for &id in self.open.iter().rev() {
            let name = match self.doc.node(id).tag_name() {
                Some(n) => n,
                None => continue,
            };
            if name == tag {
                return true;
            }
            if SCOPE_BOUNDARY.contains(&name) {
                return false;
            }
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
            if except == Some(name.as_str()) {
                break;
            }
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
            "html", "table", "td", "th", "caption", "template", "applet", "marquee", "object",
            "button",
        ];
        let p_in_scope = self.open.iter().rev().any(|&id| {
            let name = self.doc.node(id).tag_name().unwrap_or("");
            if name == "p" {
                return true;
            }
            if P_SCOPE_BOUNDARY.contains(&name) {
                return false;
            }
            false
        });
        if p_in_scope {
            self.generate_implied_end_tags(Some("p"));
            self.pop_until("p");
        }
    }

    /// Formatting elements (HTML Living Standard §13.2.8) — the set the
    /// adoption agency algorithm reconstructs when they are misnested.
    /// §13.2.8: the elements in the "special" category.
    ///
    /// The "any other end tag" handling walks the stack from the top and
    /// gives up at a special element: the end tag is a parse error there
    /// and nothing is popped.
    const SPECIAL_ELEMENTS: &[&str] = &[
        "address", "applet", "area", "article", "aside", "base", "basefont", "bgsound",
        "blockquote", "body", "br", "button", "caption", "center", "col", "colgroup", "dd",
        "details", "dir", "div", "dl", "dt", "embed", "fieldset", "figcaption", "figure",
        "footer", "form", "frame", "frameset", "h1", "h2", "h3", "h4", "h5", "h6", "head",
        "header", "hgroup", "hr", "html", "iframe", "img", "input", "keygen", "li", "link",
        "listing", "main", "marquee", "menu", "meta", "nav", "noembed", "noframes",
        "noscript", "object", "ol", "p", "param", "plaintext", "pre", "script", "section",
        "select", "source", "style", "summary", "table", "tbody", "td", "template",
        "textarea", "tfoot", "th", "thead", "title", "tr", "track", "ul", "wbr", "xmp",
    ];

    const FORMATTING_ELEMENTS: &[&str] = &[
        "a", "b", "big", "code", "em", "font", "i", "nobr", "s", "small", "strike", "strong", "tt",
        "u",
    ];

    /// Is `name` a formatting element (§13.2.6)?
    fn is_formatting_element(name: &str) -> bool {
        Self::FORMATTING_ELEMENTS.contains(&name)
    }

    /// Push a marker onto the list of active formatting elements (§13.2.6).
    fn push_afe_marker(&mut self) {
        if !self.afe.is_empty() {
            self.afe.push(AfeEntry {
                name: "#marker".to_string(),
                marker: true,
                id: None,
            });
        }
    }

    /// Remove the most recent entry for `name` from the list of active
    /// formatting elements — called when an element is popped off the stack
    /// of open elements (§13.2.6).
    fn remove_afe(&mut self, name: &str) {
        if let Some(i) = self.afe.iter().rposition(|e| !e.marker && e.name == name) {
            self.afe.remove(i);
        }
    }

    /// The `<a>` special case (§13.2.6.4.7): find an `<a>` in the list of
    /// active formatting elements between the end of the list and the last
    /// marker (or the start of the list if there is no marker).
    fn find_active_anchor(&self) -> Option<usize> {
        for i in (0..self.afe.len()).rev() {
            if self.afe[i].marker {
                return None;
            }
            if self.afe[i].name == "a" {
                return Some(i);
            }
        }
        None
    }

    /// §13.2.6.4 step 7: find the formatting element the outer loop
    /// works on — the LAST entry in the list of active formatting
    /// elements that has the same tag name, searching backwards from the
    /// end of the list and stopping at the last marker.
    ///
    /// The marker is the boundary: formatting that was separated from the
    /// end of the list by a non-formatting element is out of scope for
    /// the formatting rules, and this search never reaches past it.
    fn find_formatting_element(&self, tag: &str) -> Option<usize> {
        for i in (0..self.afe.len()).rev() {
            if self.afe[i].marker {
                // The last marker bounds the search.
                return None;
            }
            if self.afe[i].name == tag {
                return Some(i);
            }
        }
        None
    }

    /// §13.2.6.4: the OUTER loop of the adoption agency algorithm.
    ///
    /// The outer loop is where the list of active formatting elements and
    /// the stack of open elements get compared, and where the spec's
    /// parse errors live:
    ///
    /// * step 5 — the eight-iteration cap,
    /// * step 7 — find the element in the list, bounded by the last
    ///   marker; if there is none, abort (and act as "any other end
    ///   tag"),
    /// * step 8 — if it is not in the stack of open elements, remove it
    ///   from the list and abort,
    /// * step 9 — if it is in the stack but not in scope, abort.
    ///
    /// The inner step is passed in, so that the loop's control flow can
    /// be tested without a tree: the inner step returns `true` when it
    /// made progress and the outer loop should run again, and `false`
    /// when it is finished.
    ///
    /// Scope: the inner step in this component is the simplified one,
    /// which is terminal — it pops the target and re-opens the misnested
    /// formatting elements — so the real path runs the loop once. The
    /// spec's outer loop keeps going because the FULL inner loop
    /// re-inserts the formatting element into the list, which gives the
    /// next iteration something new to find. That is the next slice.
    fn adoption_agency_outer(
        &mut self,
        tag: &str,
        inner: &mut dyn FnMut(&mut TreeBuilder, NodeId) -> bool,
    ) -> OuterOutcome {
        let mut outer_loop_counter: u32 = 0;
        loop {
            // Step 5: the cap.
            if outer_loop_counter >= 8 {
                return OuterOutcome::IterationLimit;
            }
            outer_loop_counter += 1;

            // Step 7: the element in the list, bounded by the last marker.
            let Some(idx) = self.find_formatting_element(tag) else {
                return OuterOutcome::NoSuchElement;
            };
            let Some(id) = self.afe[idx].id else {
                return OuterOutcome::NoSuchElement;
            };

            // Step 8: not in the stack of open elements.
            if !self.open.contains(&id) {
                self.afe.remove(idx);
                self.doc.errors.push(
                    "adoption-agency-1.2: the formatting element is not in the stack of open \
                     elements"
                        .into(),
                );
                return OuterOutcome::NotInOpenStack;
            }

            // Step 9: in the stack, but not in scope.
            if !self.in_scope(tag) {
                self.doc
                    .errors
                    .push("adoption-agency-4.4: the formatting element is not in scope".into());
                return OuterOutcome::NotInScope;
            }

            // html5lib's step 9: if the formatting element is not the
            // current node, that is a parse error -- but the algorithm
            // goes on regardless, and so does this one.
            if self.open.last() != Some(&id) {
                self.doc.errors.push(
                    "adoption-agency-1.3: the formatting element is not the current node".into(),
                );
            }

            // Steps 10+: the inner loop.
            if !inner(self, id) {
                // §13.2.8: when the inner step is finished, the outer
                // loop removes the formatting element from the list of
                // active formatting elements. The entry is found by the
                // element's id, not by the index: the inner step can
                // shrink the list (the Noah's Ark clause fires while
                // it re-opens elements), and a stale index would remove
                // the wrong entry -- or none at all.
                if let Some(pos) = self.afe.iter().position(|e| e.id == Some(id)) {
                    self.afe.remove(pos);
                }
                return OuterOutcome::InnerCompleted;
            }
        }
    }

    /// Apply the adoption agency algorithm (§13.2.8.3) to a formatting
    /// element end tag.
    ///
    /// The target element is often *not* the current node: in `<b><i>x</b>` the
    /// `</b>` closes `<b>` while `<i>` is still open inside it. The spec's
    /// recovery is to re-open the misnested formatting elements (with their
    /// original attributes) once the target has closed, so the content that
    /// follows the stray end tag stays *inside* them:
    ///
    /// ```text
    /// <p>1<b>2<i>3</b>4</i>5</p>  =>  <p>1<b>2<i>3</i></b><i>4</i>5</p>
    /// ```
    ///
    /// Simply popping the stack to the target loses the formatting entirely
    /// (`<p>1<b>2<i>3</i></b>45</p>`), which is what this used to do.
    ///
    /// Scope: this implements the re-opening step for misnested formatting
    /// elements. The full algorithm's list-of-active-formatting-elements
    /// bookkeeping, and its `<a>` special case, are still not implemented.
    /// §13.2.8: the adoption agency algorithm, as the tree runs it.
    ///
    /// The outer loop (slice 5) decides whether the inner step runs: it
    /// searches the list of active formatting elements for the subject,
    /// bounded by the last marker, and stops when the element is not on
    /// the stack of open elements or is out of scope. The inner step is
    /// this component's re-opening of the misnested formatting elements.
    ///
    /// When the outer loop finds no formatting element with that name,
    /// §13.2.8 says to act as described in the "any other end tag"
    /// entry -- which is not the same as popping to the tag: the walk
    /// stops at a *special* element.
    fn adoption_agency_wired(&mut self, tag: &str) -> OuterOutcome {
        let outcome = self.adoption_agency_outer(tag, &mut |b, id| {
            b.adoption_agency(tag, id)
        });
        if matches!(outcome, OuterOutcome::NoSuchElement) {
            self.any_other_end_tag(tag);
        }
        outcome
    }

    /// §13.2.8: the "any other end tag" handling.
    ///
    /// The end tag's name is compared against the elements on the stack,
    /// from the top down. If it matches, the stack is popped down to and
    /// including that element. If a *special* element comes first, the
    /// search gives up there and nothing is popped.
    fn any_other_end_tag(&mut self, tag: &str) {
        for &id in self.open.iter().rev() {
            let name = match self.doc.node(id).tag_name() {
                Some(n) => n,
                None => continue,
            };
            if name == tag {
                self.generate_implied_end_tags(Some(tag));
                while let Some(&top) = self.open.last() {
                    self.open.pop();
                    if top == id {
                        break;
                    }
                }
                return;
            }
            if Self::SPECIAL_ELEMENTS.contains(&name) {
                // html5lib records the special element that got in the way.
                self.doc
                    .errors
                    .push(format!("unexpected-end-tag: {name}"));
                return;
            }
        }
    }

    /// §13.2.8.3: the INNER step of the adoption agency algorithm, in two
    /// paths.
    ///
    /// **The furthest-block path** (html5lib's `endTagFormatting`, steps 5
    /// through 15) runs when there is a *special* element at or above the
    /// formatting element in the stack of open elements -- that is, when a
    /// block interrupted the formatting element and is still open. The
    /// formatting element is then cloned, the block's children move into
    /// the clone, and the clone is re-opened inside the block: the
    /// formatting survives the block boundary.
    ///
    /// **The no-furthest-block path** (the component's existing behaviour)
    /// runs when there is none: the stack is popped to the target and the
    /// misnested formatting elements above it are re-opened.
    ///
    /// The return value is the outer loop's "made progress" flag: the
    /// furthest-block path leaves the clone open on the stack, so the
    /// outer loop must run again to close it.
    fn adoption_agency(&mut self, tag: &str, fmt_id: NodeId) -> bool {
        // The formatting element's place in the stack of open elements.
        let Some(stack_idx) = self.open.iter().position(|&id| id == fmt_id) else {
            return false;
        };

        // Step 5: the furthest block -- the first special element at or
        // above the formatting element in the stack of open elements.
        let furthest = self.open[stack_idx..].iter().copied().find(|&id| {
            self.doc
                .node(id)
                .tag_name()
                .map(|n| Self::SPECIAL_ELEMENTS.contains(&n))
                .unwrap_or(false)
        });

        let Some(furthest_id) = furthest else {
            // No furthest block: the existing path.
            self.adoption_agency_no_furthest_block(tag);
            return false;
        };

        // Step 7: the common ancestor -- the element immediately below the
        // formatting element in the stack of open elements.
        let Some(common_id) = stack_idx.checked_sub(1).map(|i| self.open[i]) else {
            return false;
        };

        // Step 8: the bookmark -- the list position of the formatting
        // element, which is where its clone will go.
        let mut bookmark = self.find_formatting_element(tag).unwrap_or(0);

        // Step 9: the inner loop.
        let mut last_node = furthest_id;
        let mut node = furthest_id;
        let mut index = self
            .open
            .iter()
            .position(|&id| id == furthest_id)
            .unwrap_or(stack_idx);
        let mut inner = 0u32;
        while inner < 3 {
            inner += 1;
            if index == 0 {
                break;
            }
            index -= 1;
            node = self.open[index];
            // Not in the list of active formatting elements: remove it from
            // the stack and go round again.
            if !self.afe.iter().any(|e| e.id == Some(node)) {
                self.open.remove(index);
                continue;
            }
            // Step 9.6: the formatting element itself.
            if node == fmt_id {
                break;
            }
            // Step 9.7: the bookmark moves after the node.
            if last_node == furthest_id {
                if let Some(pos) = self.afe.iter().position(|e| e.id == Some(node)) {
                    bookmark = pos + 1;
                }
            }
            // Step 9.8: clone the node; the clone replaces it in both the
            // list and the stack.
            let Some(clone) = self.doc.clone_element(node) else {
                break;
            };
            if let Some(pos) = self.afe.iter().position(|e| e.id == Some(node)) {
                self.afe[pos].id = Some(clone);
            }
            if let Some(pos) = self.open.iter().position(|&id| id == node) {
                self.open[pos] = clone;
            }
            node = clone;
            // Step 9.9: the last node moves into the clone.
            self.doc.detach_from_parent(last_node);
            self.doc.append_child(clone, last_node);
            // Step 9.10.
            last_node = node;
        }

        // Step 10: the last node goes into the common ancestor. (html5lib
        // foster-parents it when the common ancestor is a table; this
        // component has no table insertion modes, so that cannot happen.)
        self.doc.detach_from_parent(last_node);
        self.doc.append_child(common_id, last_node);

        // Step 11: the clone of the formatting element.
        let Some(clone) = self.doc.clone_element(fmt_id) else {
            return false;
        };
        // Step 12: the furthest block's children move into the clone.
        self.doc.reparent_children(furthest_id, clone);
        // Step 13: the clone goes into the furthest block.
        self.doc.append_child(furthest_id, clone);

        // Step 14: in the list of active formatting elements, the clone
        // replaces the formatting element, at the bookmark.
        self.afe.retain(|e| e.id != Some(fmt_id));
        let entry = AfeEntry {
            name: tag.to_string(),
            marker: false,
            id: Some(clone),
        };
        let at = bookmark.min(self.afe.len());
        self.afe.insert(at, entry);

        // Step 15: in the stack of open elements, the clone replaces the
        // formatting element, immediately above the furthest block.
        self.open.retain(|&id| id != fmt_id);
        let pos = self
            .open
            .iter()
            .position(|&id| id == furthest_id)
            .map(|i| i + 1)
            .unwrap_or(self.open.len());
        self.open.insert(pos, clone);

        // The clone is left open: the outer loop runs again and closes it.
        true
    }

    /// The adoption agency's inner step for the case where there is **no**
    /// furthest block: pop the stack to the target, and re-open the
    /// misnested formatting elements above it so the content that follows
    /// the stray end tag stays inside them.
    fn adoption_agency_no_furthest_block(&mut self, tag: &str) {
        let Some(target_idx) = self
            .open
            .iter()
            .rposition(|&id| self.doc.node(id).tag_name() == Some(tag))
        else {
            // Not in scope — the caller has already logged the parse error.
            return;
        };

        // The formatting elements misnested above the target, in stack order.
        let misnested: Vec<(String, Vec<Attribute>)> = self.open[target_idx + 1..]
            .iter()
            .filter_map(|&id| {
                let node = self.doc.node(id);
                let name = node.tag_name()?;
                if !Self::FORMATTING_ELEMENTS.contains(&name) {
                    return None;
                }
                let attrs = node.element_data()?.attrs.clone();
                Some((name.to_string(), attrs))
            })
            .collect();

        self.generate_implied_end_tags(Some(tag));
        self.pop_until(tag);

        // Reconstruct the misnested formatting elements so the content that
        // follows the stray end tag stays inside them.
        for (name, attrs) in misnested {
            self.insert_and_push(&name, attrs);
        }
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
        if let Some(id) = self.head_ptr {
            return id;
        }
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
            Token::Doctype {
                name,
                public_id,
                system_id,
                force_quirks,
            } => {
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
            Token::Comment(c) => {
                self.insert_comment(c);
            }
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if name == "html" => {
                let _id = self.insert_and_push("html", attrs.clone());
                self.mode = InsertionMode::BeforeHead;
            }
            Token::EndTag { ref name }
                if !matches!(name.as_str(), "head" | "body" | "html" | "br") =>
            {
                // Parse error — ignore
                self.doc
                    .errors
                    .push(format!("unexpected end tag </{name}> before <html>"));
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
            Token::Comment(c) => {
                self.insert_comment(c);
            }
            Token::StartTag { ref name, .. } if name == "html" => {
                self.process_in_body(token); // spec: process as in body
            }
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if name == "head" => {
                let id = self.insert_and_push("head", attrs.clone());
                self.head_ptr = Some(id);
                self.mode = InsertionMode::InHead;
            }
            Token::EndTag { ref name }
                if !matches!(name.as_str(), "head" | "body" | "html" | "br") =>
            {
                self.doc
                    .errors
                    .push(format!("unexpected end tag </{name}> before head"));
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
            Token::Comment(c) => {
                self.insert_comment(c);
            }
            Token::StartTag { ref name, .. } if name == "html" => {
                self.process_in_body(token);
            }
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if matches!(
                name.as_str(),
                "base" | "basefont" | "bgsound" | "link" | "meta"
            ) =>
            {
                // Void elements in head — insert and don't push
                self.insert_element(name, attrs.clone());
            }
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if name == "title" => {
                self.insert_and_push("title", attrs.clone());
                self.mode = InsertionMode::Text;
            }
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if matches!(name.as_str(), "noframes" | "style") => {
                self.insert_and_push(name, attrs.clone());
                self.mode = InsertionMode::Text;
            }
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if name == "noscript" => {
                self.insert_and_push("noscript", attrs.clone());
                // simplified: treat contents as text (no scripting)
            }
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if name == "script" => {
                self.insert_and_push("script", attrs.clone());
                self.mode = InsertionMode::Text;
            }
            Token::EndTag { ref name } if name == "head" => {
                self.pop(); // pop <head>
                self.mode = InsertionMode::AfterHead;
            }
            Token::EndTag { ref name } if matches!(name.as_str(), "body" | "html" | "br") => {
                // Implied </head> then reprocess
                self.pop(); // pop head
                self.mode = InsertionMode::AfterHead;
                self.process(token);
            }
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if name == "template" => {
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
                self.doc
                    .errors
                    .push(format!("unexpected end tag </{name}> in head"));
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
            Token::Comment(c) => {
                self.insert_comment(c);
            }
            Token::StartTag { ref name, .. } if name == "html" => {
                self.process_in_body(token);
            }
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if name == "body" => {
                self.insert_and_push("body", attrs.clone());
                self.frameset_ok = false;
                self.mode = InsertionMode::InBody;
            }
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if name == "frameset" => {
                self.insert_and_push("frameset", attrs.clone());
                self.mode = InsertionMode::InBody; // simplified
            }
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if matches!(
                name.as_str(),
                "base"
                    | "basefont"
                    | "bgsound"
                    | "link"
                    | "meta"
                    | "noframes"
                    | "script"
                    | "style"
                    | "template"
                    | "title"
            ) =>
            {
                // These should have been in head — insert into head if available
                self.doc
                    .errors
                    .push(format!("misplaced head element <{name}> after head"));
                if let Some(head_id) = self.head_ptr {
                    self.open.push(head_id);
                    self.process_in_head(token);
                    self.open.retain(|&id| id != head_id);
                }
            }
            Token::EndTag { ref name } if name == "template" => {
                self.process_in_head(token);
            }
            Token::EndTag { ref name } if !matches!(name.as_str(), "body" | "html" | "br") => {
                self.doc
                    .errors
                    .push(format!("unexpected end tag </{name}> after head"));
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
                self.doc
                    .errors
                    .push("unexpected-null-character in body".into());
            }
            Token::Text(t) => {
                self.insert_text(&t);
                self.frameset_ok = false;
            }
            Token::Comment(c) => {
                self.insert_comment(c);
            }
            Token::Doctype { .. } => {
                self.doc.errors.push("unexpected DOCTYPE in body".into());
            }

            // ── Start tags ──────────────────────────────────────────────
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if name == "html" => {
                self.doc.errors.push("unexpected <html> in body".into());
                // Merge attributes into existing html element (simplified: ignore)
            }
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if matches!(
                name.as_str(),
                "base"
                    | "basefont"
                    | "bgsound"
                    | "link"
                    | "meta"
                    | "noframes"
                    | "script"
                    | "style"
                    | "template"
                    | "title"
            ) =>
            {
                self.process_in_head(token);
            }
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if name == "body" => {
                self.doc.errors.push("unexpected <body> in body".into());
                // Merge attributes — simplified: ignore
            }
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if name == "frameset" => {
                self.doc.errors.push("unexpected <frameset> in body".into());
            }

            // Block elements that close an open <p>
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if is_p_closing_start_tag(name) => {
                self.close_p_if_open();
                self.insert_and_push(name, attrs.clone());
            }

            // Heading elements close other headings and open <p>
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if matches!(name.as_str(), "h1" | "h2" | "h3" | "h4" | "h5" | "h6") => {
                self.close_p_if_open();
                // Close any open heading
                let top = self.current_node();
                if let Some(tag) = self.doc.node(top).tag_name() {
                    if matches!(tag, "h1" | "h2" | "h3" | "h4" | "h5" | "h6") {
                        self.doc
                            .errors
                            .push(format!("heading inside heading: <{name}>"));
                        self.pop();
                    }
                }
                self.insert_and_push(name, attrs.clone());
            }

            // pre, listing: close p, set frameset-ok = false
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if matches!(name.as_str(), "pre" | "listing") => {
                self.close_p_if_open();
                self.insert_and_push(name, attrs.clone());
                self.frameset_ok = false;
                // Skip a leading newline in next token (handled by merging)
            }

            // form: only one active form at a time (simplified)
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if name == "form" => {
                self.close_p_if_open();
                self.insert_and_push("form", attrs.clone());
            }

            // li: close any open li
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if name == "li" => {
                self.frameset_ok = false;
                // Close any open li on the stack
                if self.in_open_stack("li") {
                    self.generate_implied_end_tags(Some("li"));
                    if self.doc.node(self.current_node()).tag_name() != Some("li") {
                        self.doc
                            .errors
                            .push("unexpected li inside other list item".into());
                    }
                    self.pop_until("li");
                }
                self.close_p_if_open();
                self.insert_and_push("li", attrs.clone());
            }

            // dt/dd: close any open dt or dd
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if matches!(name.as_str(), "dt" | "dd") => {
                self.frameset_ok = false;
                if self.in_open_stack("dt") || self.in_open_stack("dd") {
                    self.generate_implied_end_tags(Some(name));
                    self.pop_until_any(&["dt", "dd"]);
                }
                self.close_p_if_open();
                self.insert_and_push(name, attrs.clone());
            }

            // The `<a>` special case (§13.2.6.4.7): a start tag for `<a>` does
            // not simply nest. If an `<a>` is still in the list of active
            // formatting elements — between the end of the list and the last
            // marker — it is closed first (a parse error), so the two anchors
            // end up as siblings instead of one inside the other.
            Token::StartTag {
                ref name,
                ref attrs,
                ..
            } if name == "a" => {
                if let Some(idx) = self.find_active_anchor() {
                    self.doc.errors.push(
                        "unexpected-start-tag-implies-end-tag: the nested <a> start tag closes \
                         the active anchor"
                            .into(),
                    );
                    let anchor_id = self.afe[idx].id;
                    self.afe.remove(idx);
                    if let Some(anchor_id) = anchor_id {
                        self.adoption_agency("a", anchor_id);
                    }
                }
                self.insert_and_push(name, attrs.clone());
            }

            // Void elements and formatting elements that don't push
            Token::StartTag {
                ref name,
                ref attrs,
                self_closing,
            } => {
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
            Token::EndTag { ref name } if is_p_closing_end_tag(name) => {
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
                    .iter()
                    .any(|h| self.in_scope(h));
                if !in_scope {
                    self.doc.errors.push(format!("</{name}> not in scope"));
                    return;
                }
                self.generate_implied_end_tags(Some(name));
                self.pop_until_any(&["h1", "h2", "h3", "h4", "h5", "h6"]);
            }

            // Formatting element end tags: simplified adoption agency
            Token::EndTag { ref name }
                if matches!(
                    name.as_str(),
                    "a" | "b"
                        | "big"
                        | "code"
                        | "em"
                        | "font"
                        | "i"
                        | "nobr"
                        | "s"
                        | "small"
                        | "strike"
                        | "strong"
                        | "tt"
                        | "u"
                ) =>
            {
                self.adoption_agency_wired(name);
            }

            // Generic end tags
            Token::EndTag { ref name } => {
                // Pop up to (and including) the matching open element
                if self.in_open_stack(name) {
                    self.generate_implied_end_tags(Some(name));
                    self.pop_until(name);
                } else {
                    self.doc
                        .errors
                        .push(format!("end tag </{name}> with no matching open element"));
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
            Token::Text(t) => {
                self.insert_text(&t);
            }
            Token::Eof => {
                self.doc.errors.push("EOF in text/raw-text element".into());
                self.pop();
                self.mode = InsertionMode::InBody;
            }
            Token::EndTag { .. } => {
                self.pop();
                // Return to InBody (or AfterHead for title/style in head)
                if self.doc.find_element("body").is_some()
                    && self
                        .open
                        .iter()
                        .any(|&id| self.doc.node(id).tag_name() == Some("body"))
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
                self.doc
                    .errors
                    .push("unexpected token after </html>".into());
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
    matches!(
        name,
        "address"
            | "article"
            | "aside"
            | "blockquote"
            | "center"
            | "details"
            | "dialog"
            | "dir"
            | "div"
            | "dl"
            | "fieldset"
            | "figcaption"
            | "figure"
            | "footer"
            | "header"
            | "hgroup"
            | "hr"
            | "main"
            | "menu"
            | "nav"
            | "ol"
            | "p"
            | "section"
            | "summary"
            | "table"
            | "ul"
            | "h1"
            | "h2"
            | "h3"
            | "h4"
            | "h5"
            | "h6"
            | "pre"
            | "listing"
            | "form"
    )
}

/// End tags that should close an open `<p>` first (block-level closing tags).
fn is_p_closing_end_tag(name: &str) -> bool {
    matches!(
        name,
        "address"
            | "article"
            | "aside"
            | "blockquote"
            | "button"
            | "center"
            | "details"
            | "dialog"
            | "dir"
            | "div"
            | "dl"
            | "fieldset"
            | "figcaption"
            | "figure"
            | "footer"
            | "header"
            | "hgroup"
            | "listing"
            | "main"
            | "menu"
            | "nav"
            | "ol"
            | "pre"
            | "section"
            | "summary"
            | "table"
            | "ul"
            | "p"
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::html::tokenizer::tokenize;

    /// Parse `html` and return the non-marker entries of the list of
    /// active formatting elements.
    fn afe_list(html: &str) -> Vec<String> {
        let (tokens, _errs) = tokenize(html);
        let mut builder = TreeBuilder::new();
        for token in tokens {
            builder.process(token);
        }
        builder
            .afe
            .iter()
            .filter(|e| !e.marker)
            .map(|e| e.name.clone())
            .collect()
    }

    #[test]
    fn noahs_ark_collapses_the_earliest_of_three_identical_elements() {
        // Four identical, still-empty <b>s: the list keeps three, and the
        // EARLIEST is the one that goes.
        let list = afe_list("<b><b><b><b>");
        assert_eq!(list, vec!["b", "b"]);
    }

    #[test]
    fn noahs_ark_does_not_fire_when_the_text_differs() {
        // The first three <b>s hold different text by the time the fourth
        // is inserted, so they are not "the same" and nothing is removed.
        let list = afe_list("<b>1<b>2<b>3<b>4");
        assert_eq!(list, vec!["b", "b", "b", "b"]);
    }

    #[test]
    fn noahs_ark_compares_attributes() {
        // The second <b> has an attribute, so it is never a candidate for
        // the clause. The first, third and fourth are identical, so the
        // clause fires once and removes the EARLIEST of them -- leaving
        // three entries, not two. An implementation that ignored
        // attributes would collapse all four and leave two.
        let list = afe_list("<b><b class=\"x\"><b><b>");
        assert_eq!(list, vec!["b", "b", "b"]);
    }

    #[test]
    fn noahs_ark_treats_identical_attributes_as_identical() {
        let list = afe_list("<b class=\"x\"><b class=\"x\"><b class=\"x\"><b class=\"x\">");
        assert_eq!(list, vec!["b", "b"]);
    }

    #[test]
    fn the_clause_only_touches_formatting_elements() {
        // <div> is not a formatting element, so it never enters the list
        // and the clause has nothing to work on.
        let list = afe_list("<div><div><div><div>");
        assert!(list.is_empty());
    }

    /// Build an AFE entry for `name` referring to `id`.
    fn entry(name: &str, id: Option<NodeId>) -> AfeEntry {
        AfeEntry {
            name: name.to_string(),
            marker: false,
            id,
        }
    }

    #[test]
    fn the_search_finds_the_last_matching_entry() {
        let mut b = TreeBuilder::new();
        let first = b.insert_element("b", vec![]);
        let inner = b.insert_element("i", vec![]);
        let last = b.insert_element("b", vec![]);
        b.afe.push(entry("b", Some(first)));
        b.afe.push(entry("i", Some(inner)));
        b.afe.push(entry("b", Some(last)));
        assert_eq!(b.find_formatting_element("b"), Some(2));
        assert_eq!(b.find_formatting_element("i"), Some(1));
        assert_eq!(b.find_formatting_element("u"), None);
    }

    #[test]
    fn the_search_stops_at_the_last_marker() {
        let mut b = TreeBuilder::new();
        let bold = b.insert_element("b", vec![]);
        let ital = b.insert_element("i", vec![]);
        b.afe.push(entry("b", Some(bold)));
        b.afe.push(AfeEntry {
            name: "#marker".to_string(),
            marker: true,
            id: None,
        });
        b.afe.push(entry("i", Some(ital)));
        // The <b> is behind the marker, so the search must not find it.
        assert_eq!(b.find_formatting_element("b"), None);
        // The <i> is in front of the marker.
        assert_eq!(b.find_formatting_element("i"), Some(2));
    }

    #[test]
    fn an_element_that_is_not_on_the_stack_is_removed_from_the_list() {
        let mut b = TreeBuilder::new();
        // insert_element does not push onto the stack, so this <b> is on
        // the list but not in the stack of open elements.
        let bold = b.insert_element("b", vec![]);
        b.afe.push(entry("b", Some(bold)));
        let outcome = b.adoption_agency_outer("b", &mut |_, _| false);
        assert_eq!(outcome, OuterOutcome::NotInOpenStack);
        assert!(b.afe.is_empty(), "the entry must be removed from the list");
        assert!(
            b.doc
                .errors
                .iter()
                .any(|e| e.starts_with("adoption-agency-1.2")),
            "the parse error must be recorded: {:?}",
            b.doc.errors
        );
    }

    #[test]
    fn the_formatting_element_that_is_not_the_current_node_records_the_spec_error() {
        let mut b = TreeBuilder::new();
        let bold = b.insert_element("b", vec![]);
        let ital = b.insert_element("i", vec![]);
        b.open.push(bold);
        b.open.push(ital); // the <i> is the current node, not the <b>
        b.afe.push(entry("b", Some(bold)));
        let outcome = b.adoption_agency_outer("b", &mut |_, _| false);
        assert_eq!(outcome, OuterOutcome::InnerCompleted);
        assert!(
            b.doc
                .errors
                .iter()
                .any(|e| e.starts_with("adoption-agency-1.3")),
            "the parse error must be recorded: {:?}",
            b.doc.errors
        );
    }

    #[test]
    fn the_fallback_records_the_unexpected_end_tag() {
        let mut b = TreeBuilder::new();
        let para = b.insert_element("p", vec![]);
        b.open.push(para); // a special element gets in the way
        let outcome = b.adoption_agency_wired("b");
        assert_eq!(outcome, OuterOutcome::NoSuchElement);
        assert!(
            b.doc
                .errors
                .iter()
                .any(|e| e.starts_with("unexpected-end-tag")),
            "the parse error must be recorded: {:?}",
            b.doc.errors
        );
    }

    #[test]
    fn an_element_that_is_out_of_scope_aborts_without_touching_the_list() {
        let mut b = TreeBuilder::new();
        let bold = b.insert_element("b", vec![]);
        b.open.push(bold);
        b.afe.push(entry("b", Some(bold)));
        // A table boundary puts the <b> out of scope.
        let table = b.insert_element("table", vec![]);
        b.open.push(table);
        let outcome = b.adoption_agency_outer("b", &mut |_, _| false);
        assert_eq!(outcome, OuterOutcome::NotInScope);
        assert!(
            b.doc
                .errors
                .iter()
                .any(|e| e.starts_with("adoption-agency-4.4")),
            "the parse error must be recorded: {:?}",
            b.doc.errors
        );
        assert_eq!(b.afe.len(), 1, "the list must be left alone");
    }

    #[test]
    fn the_loop_stops_at_eight_iterations() {
        let mut b = TreeBuilder::new();
        let bold = b.insert_element("b", vec![]);
        b.open.push(bold);
        b.afe.push(entry("b", Some(bold)));
        let mut iterations = 0u32;
        let outcome = b.adoption_agency_outer("b", &mut |_, _| {
            iterations += 1;
            true // always make progress, so the cap is what stops the loop
        });
        assert_eq!(outcome, OuterOutcome::IterationLimit);
        assert_eq!(iterations, 8);
    }

    #[test]
    fn a_finished_inner_step_ends_the_loop() {
        let mut b = TreeBuilder::new();
        let bold = b.insert_element("b", vec![]);
        b.open.push(bold);
        b.afe.push(entry("b", Some(bold)));
        let mut iterations = 0u32;
        let outcome = b.adoption_agency_outer("b", &mut |_, _| {
            iterations += 1;
            false // the inner step is finished
        });
        assert_eq!(outcome, OuterOutcome::InnerCompleted);
        assert_eq!(iterations, 1);
    }

// --- the adoption agency, wired into the tree (§13.2.8) ---

    #[test]
    fn the_formatting_element_leaves_the_list_when_the_inner_completes() {
        let mut b = TreeBuilder::new();
        b.insert_and_push("b", vec![]);
        assert_eq!(b.afe.len(), 1);
        let outcome = b.adoption_agency_wired("b");
        assert_eq!(outcome, OuterOutcome::InnerCompleted);
        assert!(b.afe.is_empty(), "the element must leave the list");
    }

    #[test]
    fn an_out_of_scope_element_is_left_open() {
        let mut b = TreeBuilder::new();
        let bold = b.insert_and_push("b", vec![]);
        b.insert_and_push("table", vec![]);
        // The <table> is a scope boundary, so the <b> is in the list and
        // on the stack but out of scope: the outer loop stops, and
        // nothing is popped.
        let outcome = b.adoption_agency_wired("b");
        assert_eq!(outcome, OuterOutcome::NotInScope);
        assert!(b.open.contains(&bold), "the <b> must still be open");
    }

    #[test]
    fn the_fallback_stops_at_a_special_element() {
        let mut b = TreeBuilder::new();
        let bold = b.insert_and_push("b", vec![]);
        b.insert_and_push("object", vec![]);
        // The <object> pushed a marker, so the search for the <b> is
        // bounded before it; the fallback walks the stack and gives up
        // at the special <object> without popping anything.
        //
        // This used to use a <button>, on the strength of slice 6's
        // marker set. html5lib 1.1's source says <button> pushes no
        // marker, so the marker-bound search no longer applies here --
        // and the outcome changes with it, which is the point of
        // correcting the set rather than working around it.
        let outcome = b.adoption_agency_wired("b");
        assert_eq!(outcome, OuterOutcome::NoSuchElement);
        assert!(b.open.contains(&bold), "the <b> must still be open");
    }

}

    // --- marker semantics (§13.2.6) ---

    /// The shape of the list of active formatting elements: element names,
    /// with markers shown as "#marker".
    fn list_shape(b: &TreeBuilder) -> Vec<String> {
        b.afe
            .iter()
            .map(|e| {
                if e.marker {
                    "#marker".to_string()
                } else {
                    e.name.clone()
                }
            })
            .collect()
    }

    fn shape_of(html: &str) -> Vec<String> {
        let (tokens, _e) = tokenize(html);
        let mut b = TreeBuilder::new();
        for t in tokens {
            b.process(t);
        }
        list_shape(&b)
    }

    #[test]
    fn a_plain_element_does_not_end_the_formatting_scope() {
        // The bug this slice fixes: a marker used to be pushed before
        // EVERY element, so <div> ended formatting's scope. It does not.
        assert_eq!(shape_of("<b><div>x</div>"), vec!["b"]);
        assert_eq!(shape_of("<b><div>x</div><i>y"), vec!["b", "i"]);
    }

    #[test]
    fn button_does_not_push_a_marker() {
        // html5lib 1.1's `startTagButton` inserts no marker, and the
        // spec's in-body rule for <button> does not either.
        assert_eq!(shape_of("<b><button>x</button>"), vec!["b"]);
    }

    #[test]
    fn p_does_not_push_a_marker() {
        // A `p` start tag closes an open `p` in button scope; it does not
        // mark the end of formatting's scope.
        assert_eq!(shape_of("<b><p>x"), vec!["b"]);
    }

    #[test]
    fn template_pushes_a_marker() {
        // The spec's in-body rule for <template> inserts a marker.
        assert_eq!(shape_of("<b><template>x</template>"), vec!["b", "#marker"]);
    }

    #[test]
    fn the_marker_bounds_the_search() {
        let (tokens, _e) = tokenize("<b><object>x</object><i>y");
        let mut b = TreeBuilder::new();
        for t in tokens {
            b.process(t);
        }
        // The marker sits between the <b> and the <i>, so the search for
        // a <b> stops at it -- but the <i> is still in scope.
        assert_eq!(b.find_formatting_element("b"), None);
        assert_eq!(b.find_formatting_element("i"), Some(2));
    }

    #[test]
    fn clearing_removes_everything_up_to_and_including_the_last_marker() {
        let (tokens, _e) = tokenize("<b><object>x</object><i>y");
        let mut b = TreeBuilder::new();
        for t in tokens {
            b.process(t);
        }
        assert_eq!(list_shape(&b), vec!["b", "#marker", "i"]);
        b.clear_afe_to_last_marker();
        // "up to and including the last marker": everything after the
        // marker goes, the marker goes, and what came BEFORE it stays
        // in scope.
        assert_eq!(list_shape(&b), vec!["b"]);
    

    
    }
