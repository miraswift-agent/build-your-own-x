//! Enhanced accessibility tree — the agent's primary view of a page.
//!
//! Extends `html::access` with:
//! - Full accessible-name computation including `aria-labelledby` (via id map)
//! - Interactive element catalog for agent interaction
//! - Focusable-element detection
//! - Description computation via `aria-describedby`
//!
//! # Why this matters
//!
//! The accessibility tree is how agents understand a page without rendering.
//! Every interactive element has a *name* (what is it?), a *kind* (what can
//! you do with it?), and a *value* (what is its current state?).

use std::collections::HashMap;
use std::fmt;

use crate::html::dom::{Document, NodeData, NodeId, SemanticRole, DOCUMENT_NODE_ID};
use crate::html::access::{build_access_tree, AccessTree};

// Re-export base types so callers only need `dom::access`.
pub use crate::html::access::{
    AXNode, AXRole, FormControl, FormSummary, LandmarkRole,
};

// ─── Interactive element kinds ────────────────────────────────────────────────

/// The kind of interaction an agent can perform on an element.
#[derive(Debug, Clone, PartialEq)]
pub enum InteractiveKind {
    Link,
    Button,
    TextInput,
    PasswordInput,
    NumberInput,
    EmailInput,
    SearchInput,
    Checkbox,
    Radio,
    Select,
    Textarea,
    Submit,
    Reset,
    FileInput,
    /// Anything else with a role override.
    Other(String),
}

impl InteractiveKind {
    pub fn as_str(&self) -> &str {
        match self {
            Self::Link          => "link",
            Self::Button        => "button",
            Self::TextInput     => "text-input",
            Self::PasswordInput => "password-input",
            Self::NumberInput   => "number-input",
            Self::EmailInput    => "email-input",
            Self::SearchInput   => "search-input",
            Self::Checkbox      => "checkbox",
            Self::Radio         => "radio",
            Self::Select        => "select",
            Self::Textarea      => "textarea",
            Self::Submit        => "submit",
            Self::Reset         => "reset",
            Self::FileInput     => "file-input",
            Self::Other(s)      => s.as_str(),
        }
    }
}

// ─── Interactive element ──────────────────────────────────────────────────────

/// An element an agent can interact with.
#[derive(Debug, Clone)]
pub struct InteractiveElement {
    pub node_id: NodeId,
    pub kind: InteractiveKind,
    /// Accessible name (aria-label, text content, alt, placeholder…).
    pub name: String,
    /// Current value (href for links, value/placeholder for inputs…).
    pub value: Option<String>,
    /// Whether the element can receive keyboard focus.
    pub is_focusable: bool,
    /// Whether the element is disabled.
    pub is_disabled: bool,
    /// All raw attributes (for automation).
    pub attributes: Vec<(String, String)>,
}

// ─── Enhanced access tree ─────────────────────────────────────────────────────

/// Enhanced accessibility tree with interactive element catalog.
pub struct EnhancedAccessTree {
    /// The standard accessibility tree.
    pub base: AccessTree,
    /// All interactive elements in document order.
    pub interactive: Vec<InteractiveElement>,
    /// Map from id attribute value to node id (for aria-labelledby resolution).
    pub id_map: HashMap<String, NodeId>,
}

impl EnhancedAccessTree {
    /// Headings shortcut.
    pub fn headings(&self) -> &[(u8, String)] { &self.base.headings }
    /// Links shortcut.
    pub fn links(&self) -> &[(String, String)] { &self.base.links }
    /// Forms shortcut.
    pub fn forms(&self) -> &[FormSummary] { &self.base.forms }
}

/// Build an enhanced accessibility tree from a parsed `Document`.
pub fn build_enhanced_access_tree(doc: &Document) -> EnhancedAccessTree {
    let base    = build_access_tree(doc);
    let id_map  = build_id_map(doc);
    let interactive = collect_interactive(doc, &id_map);
    EnhancedAccessTree { base, interactive, id_map }
}

// ─── ID map ───────────────────────────────────────────────────────────────────

pub fn build_id_map(doc: &Document) -> HashMap<String, NodeId> {
    doc.nodes.iter()
        .enumerate()
        .filter_map(|(id, node)| {
            node.element_data()
                .and_then(|e| e.attr("id"))
                .map(|v| (v.to_string(), id))
        })
        .collect()
}

// ─── Interactive element detection ───────────────────────────────────────────

fn collect_interactive(doc: &Document, id_map: &HashMap<String, NodeId>) -> Vec<InteractiveElement> {
    let mut result = vec![];
    let mut queue = std::collections::VecDeque::new();
    queue.push_back(DOCUMENT_NODE_ID);
    while let Some(id) = queue.pop_front() {
        if let Some(elem) = detect_interactive(doc, id, id_map) {
            result.push(elem);
        }
        for &child in &doc.nodes[id].children {
            queue.push_back(child);
        }
    }
    result
}

fn detect_interactive(
    doc: &Document,
    id: NodeId,
    id_map: &HashMap<String, NodeId>,
) -> Option<InteractiveElement> {
    let e = doc.nodes[id].element_data()?;

    let kind: InteractiveKind = match e.tag_name.as_str() {
        "a" if e.attr("href").is_some() => InteractiveKind::Link,
        "button" => InteractiveKind::Button,
        "select" => InteractiveKind::Select,
        "textarea" => InteractiveKind::Textarea,
        "input" => {
            match e.attr("type").unwrap_or("text").to_lowercase().as_str() {
                "text"           => InteractiveKind::TextInput,
                "email"          => InteractiveKind::EmailInput,
                "password"       => InteractiveKind::PasswordInput,
                "number"         => InteractiveKind::NumberInput,
                "search"         => InteractiveKind::SearchInput,
                "tel" | "url" | "date" | "time" | "datetime-local"
                | "month" | "week" | "color" => InteractiveKind::TextInput,
                "checkbox"       => InteractiveKind::Checkbox,
                "radio"          => InteractiveKind::Radio,
                "submit"         => InteractiveKind::Submit,
                "reset"          => InteractiveKind::Reset,
                "button" | "image" => InteractiveKind::Button,
                "file"           => InteractiveKind::FileInput,
                "hidden"         => return None,
                other            => InteractiveKind::Other(format!("input[{other}]")),
            }
        }
        _ => {
            // ARIA role overrides
            let role = e.attr("role").or_else(|| e.attr("aria-role"))?;
            match role {
                "button"             => InteractiveKind::Button,
                "link"               => InteractiveKind::Link,
                "checkbox"           => InteractiveKind::Checkbox,
                "radio"              => InteractiveKind::Radio,
                "textbox"            => InteractiveKind::TextInput,
                "combobox" | "listbox" => InteractiveKind::Select,
                _                    => return None,
            }
        }
    };

    let name = compute_accessible_name(doc, id, id_map);
    let value = match e.tag_name.as_str() {
        "a"    => e.attr("href").map(str::to_string),
        "input" => e.attr("value")
            .or_else(|| e.attr("placeholder"))
            .map(str::to_string),
        _ => None,
    };
    let is_disabled  = e.attr("disabled").is_some()
        || e.attr("aria-disabled").map(|v| v == "true").unwrap_or(false);
    let is_focusable = is_focusable_element(doc, id);
    let attributes   = e.attrs.iter().map(|a| (a.name.clone(), a.value.clone())).collect();

    Some(InteractiveElement { node_id: id, kind, name, value, is_focusable, is_disabled, attributes })
}

// ─── Accessible name computation ─────────────────────────────────────────────

/// Full accessible-name computation (ARIA spec §4.3).
///
/// Priority order:
/// 1. `aria-labelledby`
/// 2. `aria-label`
/// 3. Native sources (alt, value, text content)
/// 4. `title`
pub fn compute_accessible_name(
    doc: &Document,
    id: NodeId,
    id_map: &HashMap<String, NodeId>,
) -> String {
    let e = match doc.nodes[id].element_data() { Some(e) => e, None => return String::new() };

    // 1. aria-labelledby — join text of referenced elements
    if let Some(refs) = e.attr("aria-labelledby") {
        let name: String = refs.split_whitespace()
            .filter_map(|ref_id| id_map.get(ref_id))
            .map(|&nid| doc.text_content(nid).trim().to_string())
            .filter(|s| !s.is_empty())
            .collect::<Vec<_>>()
            .join(" ");
        if !name.is_empty() { return name; }
    }

    // 2. aria-label
    if let Some(label) = e.attr("aria-label") {
        let s = label.trim().to_string();
        if !s.is_empty() { return s; }
    }

    // 3. Native sources
    match e.tag_name.as_str() {
        "img" => {
            return e.attr("alt").unwrap_or("").trim().to_string();
        }
        "input" => {
            let t = e.attr("type").unwrap_or("text");
            if matches!(t, "submit" | "button" | "reset") {
                if let Some(v) = e.attr("value") {
                    let s = v.trim().to_string();
                    if !s.is_empty() { return s; }
                }
            }
            // Label lookup via for="id"
            if let Some(elem_id) = e.attr("id") {
                if let Some(label_text) = find_label_for(doc, elem_id) {
                    if !label_text.is_empty() { return label_text; }
                }
            }
            return e.attr("placeholder").unwrap_or("").trim().to_string();
        }
        "a" | "button" => {
            let text = doc.text_content(id).trim().to_string();
            if !text.is_empty() { return text; }
        }
        _ => {}
    }

    // Headings
    if matches!(e.role, SemanticRole::Heading { .. }) {
        return doc.text_content(id).trim().to_string();
    }

    // 4. title
    if let Some(t) = e.attr("title") {
        let s = t.trim().to_string();
        if !s.is_empty() { return s; }
    }

    String::new()
}

/// Find a `<label for="id">` and return its text content.
fn find_label_for(doc: &Document, target_id: &str) -> Option<String> {
    for node in &doc.nodes {
        if let NodeData::Element(e) = &node.data {
            if e.tag_name == "label" {
                if e.attr("for") == Some(target_id) {
                    let text = doc.text_content(node.id).trim().to_string();
                    return Some(text);
                }
            }
        }
    }
    None
}

// ─── Focusable detection ──────────────────────────────────────────────────────

/// Returns `true` if the element can receive keyboard focus.
pub fn is_focusable_element(doc: &Document, id: NodeId) -> bool {
    let e = match doc.nodes[id].element_data() { Some(e) => e, None => return false };

    // Explicit tabindex
    if let Some(tab) = e.attr("tabindex") {
        return tab.parse::<i32>().map(|v| v >= 0).unwrap_or(false);
    }

    // Naturally focusable (not disabled, right type)
    match e.tag_name.as_str() {
        "a"        => e.attr("href").is_some(),
        "button" | "select" | "textarea" => e.attr("disabled").is_none(),
        "input"    => {
            e.attr("disabled").is_none()
                && e.attr("type").map(|t| t != "hidden").unwrap_or(true)
        }
        "details" | "summary" => true,
        _ => false,
    }
}

// ─── Description ─────────────────────────────────────────────────────────────

/// Compute the accessible description for an element.
pub fn compute_accessible_description(
    doc: &Document,
    id: NodeId,
    id_map: &HashMap<String, NodeId>,
) -> Option<String> {
    let e = doc.nodes[id].element_data()?;

    // aria-describedby
    if let Some(refs) = e.attr("aria-describedby") {
        let desc: String = refs.split_whitespace()
            .filter_map(|ref_id| id_map.get(ref_id))
            .map(|&nid| doc.text_content(nid).trim().to_string())
            .filter(|s| !s.is_empty())
            .collect::<Vec<_>>()
            .join(" ");
        if !desc.is_empty() { return Some(desc); }
    }

    // title (if not already used as name)
    e.attr("title").map(str::to_string)
}

// ─── Display ──────────────────────────────────────────────────────────────────

impl fmt::Display for EnhancedAccessTree {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.base)?;

        if !self.interactive.is_empty() {
            writeln!(f, "\nInteractive Elements:")?;
            for elem in &self.interactive {
                let name = if elem.name.is_empty() { "(unnamed)" } else { &elem.name };
                let dis  = if elem.is_disabled { " [disabled]" } else { "" };
                let foc  = if elem.is_focusable { " (focusable)" } else { "" };
                writeln!(f, "  [{}] {}{}{}", elem.kind.as_str(), name, dis, foc)?;
                if let Some(v) = &elem.value {
                    writeln!(f, "    value={v}")?;
                }
            }
        }

        Ok(())
    }
}
