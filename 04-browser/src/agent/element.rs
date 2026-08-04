//! Element — a handle to a single DOM node, with agent-friendly accessors.

use std::sync::{Arc, Mutex};

use crate::dom::{query_selector, query_selector_all};
use crate::html::dom::{Document, NodeData, NodeId, DOCUMENT_NODE_ID};

pub struct Element {
    pub node_id: NodeId,
    pub(crate) doc: Arc<Mutex<Document>>,
}

impl Element {
    pub(crate) fn new(node_id: NodeId, doc: Arc<Mutex<Document>>) -> Self {
        Element { node_id, doc }
    }

    /// Lock the element DOM, mapping poison to an error instead of panicking.
    fn lock_doc(&self) -> Result<std::sync::MutexGuard<'_, Document>, String> {
        self.doc
            .lock()
            .map_err(|_| "page DOM mutex poisoned".to_string())
    }

    /// Log a click interaction on this element.
    pub fn click(&self) {
        let tag = self.tag_name().unwrap_or_else(|| "?".to_string());
        eprintln!("[agent:click] <{tag}> node_id={}", self.node_id);
    }

    /// Set the `value` attribute of an input/textarea (simulates typing).
    /// No-ops (with a note) if the DOM mutex is poisoned.
    pub fn type_text(&self, text: &str) {
        let Ok(mut doc) = self.lock_doc() else {
            eprintln!("[agent:type] node_id={} skipped: DOM mutex poisoned", self.node_id);
            return;
        };
        let is_input = doc.nodes[self.node_id]
            .element_data()
            .map(|e| matches!(e.tag_name.as_str(), "input" | "textarea"))
            .unwrap_or(false);
        if is_input {
            doc.set_attribute(self.node_id, "value", text);
        }
        eprintln!("[agent:type] node_id={} text={text:?}", self.node_id);
    }

    /// Collect all descendant text content.
    pub fn text_content(&self) -> String {
        let Ok(doc) = self.lock_doc() else {
            return String::new();
        };
        doc.text_content(self.node_id)
    }

    /// Return the serialised inner HTML of this element.
    pub fn inner_html(&self) -> String {
        let Ok(doc) = self.lock_doc() else {
            return String::new();
        };
        doc.inner_html(self.node_id)
    }

    /// Get a named attribute value.
    pub fn get_attribute(&self, name: &str) -> Option<String> {
        let doc = self.lock_doc().ok()?;
        doc.get_attribute(self.node_id, name).map(str::to_string)
    }

    /// Set a named attribute. No-op if the DOM mutex is poisoned.
    pub fn set_attribute(&self, name: &str, value: &str) {
        let Ok(mut doc) = self.lock_doc() else {
            return;
        };
        doc.set_attribute(self.node_id, name, value);
    }

    /// Find the first matching child element.
    pub fn query_selector(&self, css: &str) -> Option<Element> {
        let found = {
            let doc = self.lock_doc().ok()?;
            query_selector(&doc, self.node_id, css).ok().flatten()
        };
        found.map(|id| Element::new(id, Arc::clone(&self.doc)))
    }

    /// Find all matching child elements.
    pub fn query_selector_all(&self, css: &str) -> Vec<Element> {
        let ids = {
            let Ok(doc) = self.lock_doc() else {
                return Vec::new();
            };
            query_selector_all(&doc, self.node_id, css).unwrap_or_default()
        };
        ids.into_iter()
            .map(|id| Element::new(id, Arc::clone(&self.doc)))
            .collect()
    }

    /// Return `true` if this element is not hidden by `display:none`,
    /// `visibility:hidden`, or the `hidden` attribute.
    /// Poisoned mutex → treated as not visible (fail closed).
    pub fn is_visible(&self) -> bool {
        let Ok(doc) = self.lock_doc() else {
            return false;
        };
        node_is_visible(&doc, self.node_id)
    }

    /// Return `true` if this element is not `disabled`.
    /// Poisoned mutex → treated as disabled (fail closed).
    pub fn is_enabled(&self) -> bool {
        let Ok(doc) = self.lock_doc() else {
            return false;
        };
        doc.nodes[self.node_id]
            .element_data()
            .map(|e| e.attr("disabled").is_none())
            .unwrap_or(false)
    }

    /// Tag name in lowercase, e.g. `"button"`.
    pub fn tag_name(&self) -> Option<String> {
        let doc = self.lock_doc().ok()?;
        doc.nodes[self.node_id].tag_name().map(str::to_string)
    }

    /// Value of the `id` attribute.
    pub fn id(&self) -> Option<String> {
        let doc = self.lock_doc().ok()?;
        doc.get_attribute(self.node_id, "id").map(str::to_string)
    }

    /// Class names as a `Vec<String>`.
    pub fn class_list(&self) -> Vec<String> {
        let Ok(doc) = self.lock_doc() else {
            return Vec::new();
        };
        doc.class_list(self.node_id)
    }
}

impl std::fmt::Debug for Element {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let tag = self.tag_name().unwrap_or_else(|| "?".to_string());
        write!(f, "Element(<{tag}> node_id={})", self.node_id)
    }
}

/// Recursively check whether a node and all its ancestors are visible.
fn node_is_visible(doc: &Document, node_id: NodeId) -> bool {
    let node = &doc.nodes[node_id];
    if let Some(e) = node.element_data() {
        // Inline style check.
        if let Some(style) = e.attr("style") {
            let s = style.to_lowercase().replace(' ', "");
            if s.contains("display:none") || s.contains("visibility:hidden") {
                return false;
            }
        }
        // `hidden` boolean attribute.
        if e.attr("hidden").is_some() {
            return false;
        }
    }
    // Walk up.
    match node.parent {
        Some(pid) if pid != DOCUMENT_NODE_ID => node_is_visible(doc, pid),
        _ => true,
    }
}
