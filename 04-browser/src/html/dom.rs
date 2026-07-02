//! DOM types for the agent browser.
//!
//! Uses an arena-based tree: all nodes live in a `Vec<Node>` inside `Document`,
//! referenced by `NodeId` (a plain `usize`). This avoids Rc/RefCell complexity
//! and gives O(1) random access by id.
//!
//! The semantic classification (`SemanticRole`) is the key innovation vs a
//! traditional DOM: nodes are typed by *meaning* (heading, form, link, media)
//! rather than just tag name, making them immediately useful for agents.
//!
//! Stage 02 additions:
//! - `MutationKind` / `MutationRecord` — DOM change tracking
//! - `pending_mutations` on `Document` — buffer consumed by `MutationObserver`
//! - Navigation helpers, attribute methods, manipulation, serialization, queries

use std::fmt;
use std::collections::VecDeque;

/// An index into `Document::nodes`. `0` is always the document root.
pub type NodeId = usize;

/// The id of the implicit document root node.
pub const DOCUMENT_NODE_ID: NodeId = 0;

// ---------------------------------------------------------------------------
// Element categories (HTML5 spec §13.1.2)
// ---------------------------------------------------------------------------

/// The five element categories from the HTML5 parsing specification.
///
/// These categories affect parsing behaviour (e.g. void elements never get
/// child nodes, raw text elements consume content verbatim).
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ElementCategory {
    /// Cannot have children: `area base br col embed hr img input link meta param source track wbr`
    Void,
    /// `<template>` — special fragment parsing rules
    Template,
    /// Content is raw text with no markup: `script style`
    RawText,
    /// Content has entity refs but no tags: `textarea title`
    EscapableRawText,
    /// Everything else
    Normal,
}

// ---------------------------------------------------------------------------
// Semantic roles — agent-oriented classification
// ---------------------------------------------------------------------------

/// What an element *means* for an agent.
///
/// Agents don't need to know that something is a `<div>` — they need to know
/// whether it is structural scaffolding, a heading, a form field, a link, etc.
#[derive(Debug, Clone, PartialEq)]
pub enum SemanticRole {
    /// Document structure with no direct user meaning.
    /// Covers: `html head body div section article nav main aside header footer
    ///          address details summary`
    Structural,

    /// A heading at a given level (1–6).
    Heading { level: u8 },

    /// Flowing text content: `p blockquote pre code em strong b i u s …`
    TextFlow,

    /// An interactive form-related element.
    Form(FormKind),

    /// A hyperlink or external resource reference.
    /// `href` is `None` for anchors without an href.
    Link { href: Option<String> },

    /// Embedded media.
    /// `src` is `None` when the source is specified via `<source>` children.
    Media { src: Option<String>, alt: Option<String> },

    /// Non-content: `script` or `style`. Agents skip these by default.
    Script { is_style: bool },

    /// List content: `ul ol li dl dt dd menu`
    List,

    /// Tabular data: `table thead tbody tfoot tr th td caption col colgroup`
    Table,

    /// Anything that doesn't fit the above categories.
    Generic,
}

/// Distinguishes the various form-related element types.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum FormKind {
    Form,
    Input,
    Select,
    Textarea,
    Button,
    Label,
    Fieldset,
    Legend,
    /// option, optgroup, datalist, output, progress, meter
    Other,
}

// ---------------------------------------------------------------------------
// Attribute
// ---------------------------------------------------------------------------

/// A single HTML attribute `name="value"`.
#[derive(Debug, Clone, PartialEq)]
pub struct Attribute {
    pub name: String,
    pub value: String,
}

impl Attribute {
    pub fn new(name: impl Into<String>, value: impl Into<String>) -> Self {
        Attribute { name: name.into(), value: value.into() }
    }
}

// ---------------------------------------------------------------------------
// Node data
// ---------------------------------------------------------------------------

/// Per-element data including classification.
#[derive(Debug, Clone)]
pub struct ElementData {
    pub tag_name: String,
    pub attrs: Vec<Attribute>,
    pub category: ElementCategory,
    pub role: SemanticRole,
}

impl ElementData {
    /// Look up an attribute value by name (case-insensitive attribute names
    /// are already lowercased by the tokenizer).
    pub fn attr(&self, name: &str) -> Option<&str> {
        self.attrs.iter().find(|a| a.name == name).map(|a| a.value.as_str())
    }
}

/// Data for a `<!DOCTYPE …>` declaration.
#[derive(Debug, Clone)]
pub struct DoctypeData {
    pub name: String,
    pub public_id: String,
    pub system_id: String,
    pub force_quirks: bool,
}

/// The payload stored inside each `Node`.
#[derive(Debug, Clone)]
pub enum NodeData {
    Document,
    Element(ElementData),
    Text(String),
    Comment(String),
    Doctype(DoctypeData),
}

// ---------------------------------------------------------------------------
// Node
// ---------------------------------------------------------------------------

/// A single node in the document tree.
#[derive(Debug, Clone)]
pub struct Node {
    /// Stable identifier — equals the node's index in `Document::nodes`.
    pub id: NodeId,
    pub parent: Option<NodeId>,
    pub children: Vec<NodeId>,
    pub data: NodeData,
}

impl Node {
    pub fn is_element(&self) -> bool {
        matches!(self.data, NodeData::Element(_))
    }

    pub fn element_data(&self) -> Option<&ElementData> {
        if let NodeData::Element(ref e) = self.data { Some(e) } else { None }
    }

    pub fn element_data_mut(&mut self) -> Option<&mut ElementData> {
        if let NodeData::Element(ref mut e) = self.data { Some(e) } else { None }
    }

    pub fn tag_name(&self) -> Option<&str> {
        self.element_data().map(|e| e.tag_name.as_str())
    }

    pub fn is_text(&self) -> bool {
        matches!(self.data, NodeData::Text(_))
    }

    pub fn text_str(&self) -> Option<&str> {
        if let NodeData::Text(ref t) = self.data { Some(t.as_str()) } else { None }
    }
}

// ---------------------------------------------------------------------------
// Mutation records (consumed by dom::mutation::MutationObserver)
// ---------------------------------------------------------------------------

/// Which aspect of the DOM changed.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum MutationKind {
    /// The element's children changed.
    ChildList,
    /// An attribute was added, changed, or removed.
    Attributes,
    /// A text node's content changed.
    CharacterData,
}

/// A record of a single DOM change.
#[derive(Debug, Clone)]
pub struct MutationRecord {
    pub kind: MutationKind,
    pub target: NodeId,
    pub added_nodes: Vec<NodeId>,
    pub removed_nodes: Vec<NodeId>,
    pub previous_sibling: Option<NodeId>,
    pub next_sibling: Option<NodeId>,
    /// Set for `Attributes` mutations.
    pub attribute_name: Option<String>,
    /// Previous value for `Attributes` / `CharacterData` mutations.
    pub old_value: Option<String>,
}

// ---------------------------------------------------------------------------
// Document (arena)
// ---------------------------------------------------------------------------

/// The document tree — an arena of `Node`s.
///
/// Node `0` (`DOCUMENT_NODE_ID`) is always the document root.
#[derive(Debug)]
pub struct Document {
    pub nodes: Vec<Node>,
    /// Parse errors collected during tokenization / tree construction.
    pub errors: Vec<String>,
    /// Pending mutation records, consumed by `MutationObserver::take_records`.
    pub pending_mutations: Vec<MutationRecord>,
}

impl Document {
    /// Create a new empty document with only the root node.
    pub fn new() -> Self {
        let root = Node {
            id: DOCUMENT_NODE_ID,
            parent: None,
            children: vec![],
            data: NodeData::Document,
        };
        Document { nodes: vec![root], errors: vec![], pending_mutations: vec![] }
    }

    /// Allocate a new node and return its id.
    pub fn create_node(&mut self, data: NodeData) -> NodeId {
        let id = self.nodes.len();
        self.nodes.push(Node { id, parent: None, children: vec![], data });
        id
    }

    /// Append `child` as the last child of `parent`.
    pub fn append_child(&mut self, parent: NodeId, child: NodeId) {
        self.nodes[child].parent = Some(parent);
        self.nodes[parent].children.push(child);
    }

    /// Remove `child` from its current parent's children list (does not
    /// update the child's parent pointer — callers should do that).
    pub fn detach_from_parent(&mut self, child: NodeId) {
        if let Some(parent_id) = self.nodes[child].parent {
            self.nodes[parent_id].children.retain(|&c| c != child);
        }
        self.nodes[child].parent = None;
    }

    pub fn node(&self, id: NodeId) -> &Node {
        &self.nodes[id]
    }

    pub fn node_mut(&mut self, id: NodeId) -> &mut Node {
        &mut self.nodes[id]
    }

    /// Find the first element with the given tag name via BFS from the root.
    pub fn find_element(&self, tag: &str) -> Option<NodeId> {
        let mut q = VecDeque::new();
        q.push_back(DOCUMENT_NODE_ID);
        while let Some(id) = q.pop_front() {
            let node = &self.nodes[id];
            if let NodeData::Element(ref e) = node.data {
                if e.tag_name == tag { return Some(id); }
            }
            for &c in &node.children { q.push_back(c); }
        }
        None
    }

    /// Find all elements with the given tag name (BFS order).
    pub fn find_all_elements(&self, tag: &str) -> Vec<NodeId> {
        let mut result = vec![];
        let mut q = VecDeque::new();
        q.push_back(DOCUMENT_NODE_ID);
        while let Some(id) = q.pop_front() {
            let node = &self.nodes[id];
            if let NodeData::Element(ref e) = node.data {
                if e.tag_name == tag { result.push(id); }
            }
            for &c in &node.children { q.push_back(c); }
        }
        result
    }

    /// Collect all text content under `id` (recursive).
    pub fn text_content(&self, id: NodeId) -> String {
        let mut buf = String::new();
        self.collect_text(id, &mut buf);
        buf
    }

    fn collect_text(&self, id: NodeId, out: &mut String) {
        match &self.nodes[id].data {
            NodeData::Text(t) => out.push_str(t),
            _ => {
                let children: Vec<NodeId> = self.nodes[id].children.clone();
                for c in children { self.collect_text(c, out); }
            }
        }
    }

    pub fn body(&self) -> Option<NodeId> { self.find_element("body") }
    pub fn head(&self) -> Option<NodeId> { self.find_element("head") }

    // -----------------------------------------------------------------------
    // Navigation
    // -----------------------------------------------------------------------

    pub fn parent_of(&self, id: NodeId) -> Option<NodeId> {
        self.nodes[id].parent
    }

    pub fn children_of(&self, id: NodeId) -> &[NodeId] {
        &self.nodes[id].children
    }

    pub fn first_child_of(&self, id: NodeId) -> Option<NodeId> {
        self.nodes[id].children.first().copied()
    }

    pub fn last_child_of(&self, id: NodeId) -> Option<NodeId> {
        self.nodes[id].children.last().copied()
    }

    pub fn next_sibling_of(&self, id: NodeId) -> Option<NodeId> {
        let parent = self.nodes[id].parent?;
        let siblings = &self.nodes[parent].children;
        let pos = siblings.iter().position(|&c| c == id)?;
        siblings.get(pos + 1).copied()
    }

    pub fn previous_sibling_of(&self, id: NodeId) -> Option<NodeId> {
        let parent = self.nodes[id].parent?;
        let siblings = &self.nodes[parent].children;
        let pos = siblings.iter().position(|&c| c == id)?;
        if pos == 0 { None } else { Some(siblings[pos - 1]) }
    }

    // -----------------------------------------------------------------------
    // Attribute access
    // -----------------------------------------------------------------------

    pub fn get_attribute(&self, id: NodeId, name: &str) -> Option<&str> {
        self.nodes[id].element_data()?.attr(name)
    }

    pub fn set_attribute(&mut self, id: NodeId, name: &str, value: &str) {
        let old = self.get_attribute(id, name).map(str::to_string);
        if let Some(e) = self.nodes[id].element_data_mut() {
            if let Some(attr) = e.attrs.iter_mut().find(|a| a.name == name) {
                attr.value = value.to_string();
            } else {
                e.attrs.push(Attribute::new(name, value));
            }
        }
        self.pending_mutations.push(MutationRecord {
            kind: MutationKind::Attributes,
            target: id,
            added_nodes: vec![],
            removed_nodes: vec![],
            previous_sibling: None,
            next_sibling: None,
            attribute_name: Some(name.to_string()),
            old_value: old,
        });
    }

    pub fn has_attribute(&self, id: NodeId, name: &str) -> bool {
        self.get_attribute(id, name).is_some()
    }

    pub fn remove_attribute(&mut self, id: NodeId, name: &str) {
        let old = self.get_attribute(id, name).map(str::to_string);
        if let Some(e) = self.nodes[id].element_data_mut() {
            e.attrs.retain(|a| a.name != name);
        }
        if old.is_some() {
            self.pending_mutations.push(MutationRecord {
                kind: MutationKind::Attributes,
                target: id,
                added_nodes: vec![],
                removed_nodes: vec![],
                previous_sibling: None,
                next_sibling: None,
                attribute_name: Some(name.to_string()),
                old_value: old,
            });
        }
    }

    // -----------------------------------------------------------------------
    // DOM manipulation
    // -----------------------------------------------------------------------

    /// Create a new node with `data` and append it as the last child of `parent_id`.
    /// Records a ChildList mutation.
    pub fn insert_child(&mut self, parent_id: NodeId, data: NodeData) -> NodeId {
        let prev = self.last_child_of(parent_id);
        let child_id = self.create_node(data);
        self.append_child(parent_id, child_id);
        self.pending_mutations.push(MutationRecord {
            kind: MutationKind::ChildList,
            target: parent_id,
            added_nodes: vec![child_id],
            removed_nodes: vec![],
            previous_sibling: prev,
            next_sibling: None,
            attribute_name: None,
            old_value: None,
        });
        child_id
    }

    /// Remove a node from the tree. Records a ChildList mutation.
    pub fn remove_node(&mut self, child_id: NodeId) {
        let parent_id = self.nodes[child_id].parent;
        let prev = self.previous_sibling_of(child_id);
        let next = self.next_sibling_of(child_id);
        self.detach_from_parent(child_id);
        if let Some(pid) = parent_id {
            self.pending_mutations.push(MutationRecord {
                kind: MutationKind::ChildList,
                target: pid,
                added_nodes: vec![],
                removed_nodes: vec![child_id],
                previous_sibling: prev,
                next_sibling: next,
                attribute_name: None,
                old_value: None,
            });
        }
    }

    /// Create a new node and insert it before `ref_id` in `parent_id`'s children.
    pub fn insert_node_before(&mut self, parent_id: NodeId, ref_id: NodeId, data: NodeData) -> NodeId {
        let new_id = self.create_node(data);
        let prev = self.previous_sibling_of(ref_id);
        let children = &mut self.nodes[parent_id].children;
        let pos = children.iter().position(|&c| c == ref_id).unwrap_or(children.len());
        children.insert(pos, new_id);
        self.nodes[new_id].parent = Some(parent_id);
        self.pending_mutations.push(MutationRecord {
            kind: MutationKind::ChildList,
            target: parent_id,
            added_nodes: vec![new_id],
            removed_nodes: vec![],
            previous_sibling: prev,
            next_sibling: Some(ref_id),
            attribute_name: None,
            old_value: None,
        });
        new_id
    }

    /// Replace `old_id` in `parent_id`'s children with a new node.
    pub fn replace_node(&mut self, parent_id: NodeId, old_id: NodeId, data: NodeData) -> NodeId {
        let new_id = self.create_node(data);
        let prev = self.previous_sibling_of(old_id);
        let next = self.next_sibling_of(old_id);
        let children = &mut self.nodes[parent_id].children;
        if let Some(pos) = children.iter().position(|&c| c == old_id) {
            children[pos] = new_id;
        }
        self.nodes[new_id].parent = Some(parent_id);
        self.nodes[old_id].parent = None;
        self.pending_mutations.push(MutationRecord {
            kind: MutationKind::ChildList,
            target: parent_id,
            added_nodes: vec![new_id],
            removed_nodes: vec![old_id],
            previous_sibling: prev,
            next_sibling: next,
            attribute_name: None,
            old_value: None,
        });
        new_id
    }

    /// Set text content of a Text node, recording a CharacterData mutation.
    pub fn set_text_content(&mut self, id: NodeId, new_text: String) {
        if let NodeData::Text(ref t) = self.nodes[id].data {
            let old = t.clone();
            self.pending_mutations.push(MutationRecord {
                kind: MutationKind::CharacterData,
                target: id,
                added_nodes: vec![],
                removed_nodes: vec![],
                previous_sibling: None,
                next_sibling: None,
                attribute_name: None,
                old_value: Some(old),
            });
        }
        if let NodeData::Text(ref mut t) = self.nodes[id].data {
            *t = new_text;
        }
    }

    // -----------------------------------------------------------------------
    // Serialization
    // -----------------------------------------------------------------------

    /// Serialize children of `id` as an HTML string.
    pub fn inner_html(&self, id: NodeId) -> String {
        let mut buf = String::new();
        let children: Vec<NodeId> = self.nodes[id].children.clone();
        for child_id in children {
            self.serialize_node(child_id, &mut buf);
        }
        buf
    }

    /// Serialize `id` and its children as an HTML string.
    pub fn outer_html(&self, id: NodeId) -> String {
        let mut buf = String::new();
        self.serialize_node(id, &mut buf);
        buf
    }

    fn serialize_node(&self, id: NodeId, buf: &mut String) {
        let node = &self.nodes[id];
        match &node.data {
            NodeData::Document => {
                let children: Vec<NodeId> = node.children.clone();
                for c in children { self.serialize_node(c, buf); }
            }
            NodeData::Text(t) => buf.push_str(&html_escape(t)),
            NodeData::Comment(c) => {
                buf.push_str("<!--");
                buf.push_str(c);
                buf.push_str("-->");
            }
            NodeData::Doctype(d) => {
                buf.push_str("<!DOCTYPE ");
                buf.push_str(&d.name);
                buf.push('>');
            }
            NodeData::Element(e) => {
                buf.push('<');
                buf.push_str(&e.tag_name);
                for attr in &e.attrs {
                    buf.push(' ');
                    buf.push_str(&attr.name);
                    buf.push_str("=\"");
                    buf.push_str(&attr.value.replace('"', "&quot;"));
                    buf.push('"');
                }
                if e.category == ElementCategory::Void {
                    buf.push('>');
                    return;
                }
                buf.push('>');
                let children: Vec<NodeId> = node.children.clone();
                for c in children { self.serialize_node(c, buf); }
                buf.push_str("</");
                buf.push_str(&e.tag_name);
                buf.push('>');
            }
        }
    }

    // -----------------------------------------------------------------------
    // Query methods
    // -----------------------------------------------------------------------

    pub fn get_element_by_id(&self, id_val: &str) -> Option<NodeId> {
        let mut q = VecDeque::new();
        q.push_back(DOCUMENT_NODE_ID);
        while let Some(id) = q.pop_front() {
            let node = &self.nodes[id];
            if let NodeData::Element(e) = &node.data {
                if e.attr("id") == Some(id_val) { return Some(id); }
            }
            for &c in &node.children { q.push_back(c); }
        }
        None
    }

    pub fn get_elements_by_class_name(&self, class: &str) -> Vec<NodeId> {
        let mut result = vec![];
        let mut q = VecDeque::new();
        q.push_back(DOCUMENT_NODE_ID);
        while let Some(id) = q.pop_front() {
            let node = &self.nodes[id];
            if let NodeData::Element(e) = &node.data {
                let has = e.attr("class")
                    .map(|c| c.split_whitespace().any(|w| w == class))
                    .unwrap_or(false);
                if has { result.push(id); }
            }
            for &c in &node.children { q.push_back(c); }
        }
        result
    }

    pub fn get_elements_by_tag_name(&self, tag: &str) -> Vec<NodeId> {
        self.find_all_elements(tag)
    }

    // -----------------------------------------------------------------------
    // Class list helpers
    // -----------------------------------------------------------------------

    pub fn class_list(&self, id: NodeId) -> Vec<String> {
        self.nodes[id].element_data()
            .and_then(|e| e.attr("class"))
            .unwrap_or("")
            .split_whitespace()
            .map(str::to_string)
            .collect()
    }

    pub fn class_list_add(&mut self, id: NodeId, class: &str) {
        let current = self.nodes[id].element_data()
            .and_then(|e| e.attr("class"))
            .unwrap_or("")
            .to_string();
        let mut classes: Vec<&str> = current.split_whitespace().collect();
        if !classes.contains(&class) {
            classes.push(class);
            let new_val = classes.join(" ");
            self.set_attribute(id, "class", &new_val);
        }
    }

    pub fn class_list_remove(&mut self, id: NodeId, class: &str) {
        let current = self.nodes[id].element_data()
            .and_then(|e| e.attr("class"))
            .unwrap_or("")
            .to_string();
        let filtered: Vec<&str> = current.split_whitespace()
            .filter(|&c| c != class).collect();
        let new_val = filtered.join(" ");
        self.set_attribute(id, "class", &new_val);
    }

    pub fn class_list_contains(&self, id: NodeId, class: &str) -> bool {
        self.nodes[id].element_data()
            .and_then(|e| e.attr("class"))
            .map(|c| c.split_whitespace().any(|w| w == class))
            .unwrap_or(false)
    }
}

// ---------------------------------------------------------------------------
// HTML serialization helpers
// ---------------------------------------------------------------------------

fn html_escape(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for ch in s.chars() {
        match ch {
            '&'  => out.push_str("&amp;"),
            '<'  => out.push_str("&lt;"),
            '>'  => out.push_str("&gt;"),
            '"'  => out.push_str("&quot;"),
            '\'' => out.push_str("&#x27;"),
            c    => out.push(c),
        }
    }
    out
}

// ---------------------------------------------------------------------------
// Element classification
// ---------------------------------------------------------------------------

/// Derive `ElementCategory` and `SemanticRole` from a tag name and its
/// already-parsed attributes.
pub fn classify_element(tag: &str, attrs: &[Attribute]) -> (ElementCategory, SemanticRole) {
    let category = tag_category(tag);
    let role = tag_role(tag, attrs);
    (category, role)
}

fn tag_category(tag: &str) -> ElementCategory {
    match tag {
        "area" | "base" | "br" | "col" | "embed" | "hr" | "img" | "input"
        | "link" | "meta" | "param" | "source" | "track" | "wbr" => ElementCategory::Void,
        "script" | "style" => ElementCategory::RawText,
        "textarea" | "title" => ElementCategory::EscapableRawText,
        "template" => ElementCategory::Template,
        _ => ElementCategory::Normal,
    }
}

fn tag_role(tag: &str, attrs: &[Attribute]) -> SemanticRole {
    let get_attr = |name: &str| attrs.iter().find(|a| a.name == name).map(|a| a.value.clone());

    match tag {
        // Structural
        "html" | "head" | "body" | "div" | "section" | "article" | "nav"
        | "main" | "aside" | "header" | "footer" | "address" | "details"
        | "summary" | "dialog" | "slot" | "hgroup" => SemanticRole::Structural,

        // Headings
        "h1" => SemanticRole::Heading { level: 1 },
        "h2" => SemanticRole::Heading { level: 2 },
        "h3" => SemanticRole::Heading { level: 3 },
        "h4" => SemanticRole::Heading { level: 4 },
        "h5" => SemanticRole::Heading { level: 5 },
        "h6" => SemanticRole::Heading { level: 6 },

        // Text flow
        "p" | "blockquote" | "pre" | "code" | "em" | "strong" | "b" | "i"
        | "u" | "s" | "small" | "mark" | "del" | "ins" | "sub" | "sup"
        | "abbr" | "cite" | "q" | "time" | "kbd" | "var" | "samp" | "bdi"
        | "bdo" | "ruby" | "rt" | "rp" | "br" | "hr" | "span" | "wbr" => SemanticRole::TextFlow,

        // Form
        "form"     => SemanticRole::Form(FormKind::Form),
        "input"    => SemanticRole::Form(FormKind::Input),
        "select"   => SemanticRole::Form(FormKind::Select),
        "textarea" => SemanticRole::Form(FormKind::Textarea),
        "button"   => SemanticRole::Form(FormKind::Button),
        "label"    => SemanticRole::Form(FormKind::Label),
        "fieldset" => SemanticRole::Form(FormKind::Fieldset),
        "legend"   => SemanticRole::Form(FormKind::Legend),
        "option" | "optgroup" | "datalist" | "output" | "progress" | "meter"
            => SemanticRole::Form(FormKind::Other),

        // Links
        "a" | "link" => SemanticRole::Link { href: get_attr("href") },

        // Media
        "img" => SemanticRole::Media { src: get_attr("src"), alt: get_attr("alt") },
        "video" | "audio" | "canvas" | "picture" | "figure" | "figcaption"
        | "map" | "area" | "object" | "embed" | "iframe" | "source" | "track"
            => SemanticRole::Media { src: get_attr("src"), alt: get_attr("alt") },

        // Non-content
        "script" => SemanticRole::Script { is_style: false },
        "style"  => SemanticRole::Script { is_style: true },

        // Lists
        "ul" | "ol" | "li" | "dl" | "dt" | "dd" | "menu" => SemanticRole::List,

        // Tables
        "table" | "thead" | "tbody" | "tfoot" | "tr" | "th" | "td"
        | "caption" | "col" | "colgroup" => SemanticRole::Table,

        _ => SemanticRole::Generic,
    }
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

impl fmt::Display for Document {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write_node(self, DOCUMENT_NODE_ID, f, 0)
    }
}

fn write_node(doc: &Document, id: NodeId, f: &mut fmt::Formatter<'_>, depth: usize) -> fmt::Result {
    let indent = "  ".repeat(depth);
    let node = doc.node(id);
    match &node.data {
        NodeData::Document => {
            writeln!(f, "Document")?;
            for &c in &node.children { write_node(doc, c, f, depth + 1)?; }
        }
        NodeData::Doctype(d) => {
            writeln!(f, "{indent}<!DOCTYPE {}>", d.name)?;
        }
        NodeData::Comment(c) => {
            let snippet: String = c.chars().take(60).collect();
            writeln!(f, "{indent}<!-- {snippet} -->")?;
        }
        NodeData::Text(t) => {
            let trimmed = t.trim();
            if !trimmed.is_empty() {
                let snippet: String = trimmed.chars().take(80).collect();
                writeln!(f, "{indent}\"{snippet}\"")?;
            }
        }
        NodeData::Element(e) => {
            let role_hint = match &e.role {
                SemanticRole::Heading { level } => format!(" [h{level}]"),
                SemanticRole::Form(k) => format!(" [form:{k:?}]"),
                SemanticRole::Link { href: Some(h) } => format!(" [→ {h}]"),
                SemanticRole::Link { href: None } => " [→ ?]".to_string(),
                SemanticRole::Script { is_style: true } => " [style]".to_string(),
                SemanticRole::Script { is_style: false } => " [script]".to_string(),
                SemanticRole::Media { src: Some(s), .. } => format!(" [media: {s}]"),
                _ => String::new(),
            };
            writeln!(f, "{indent}<{}>{role_hint}", e.tag_name)?;
            for &c in &node.children { write_node(doc, c, f, depth + 1)?; }
        }
    }
    Ok(())
}
