//! Query API — CSS, ARIA role, text, input, and link lookups on a Page.

use std::collections::VecDeque;

use crate::dom::{query_selector, query_selector_all};
use crate::html::dom::{NodeData, NodeId, DOCUMENT_NODE_ID};

use super::element::Element;
use super::page::Page;

impl Page {
    /// Find the first element matching a CSS selector.
    pub fn query(&self, selector: &str) -> Option<Element> {
        let found = {
            // Poisoned mutex → no match (same surface as empty doc; no panic).
            let doc = self.lock_doc().ok()?;
            query_selector(&doc, DOCUMENT_NODE_ID, selector)
                .ok()
                .flatten()
        };
        found.map(|id| self.make_element(id))
    }

    /// Find all elements matching a CSS selector.
    pub fn query_all(&self, selector: &str) -> Vec<Element> {
        let ids = {
            let Ok(doc) = self.lock_doc() else {
                return Vec::new();
            };
            query_selector_all(&doc, DOCUMENT_NODE_ID, selector).unwrap_or_default()
        };
        ids.into_iter().map(|id| self.make_element(id)).collect()
    }

    /// Find all elements with a given ARIA role (explicit `role=` or implicit tag role).
    pub fn query_role(&self, role: &str) -> Vec<Element> {
        let role_lower = role.to_lowercase();
        let ids = {
            let Ok(doc) = self.lock_doc() else {
                return Vec::new();
            };
            let mut result = Vec::new();
            let mut queue: VecDeque<NodeId> = VecDeque::new();
            queue.push_back(DOCUMENT_NODE_ID);
            while let Some(id) = queue.pop_front() {
                let node = &doc.nodes[id];
                if let NodeData::Element(e) = &node.data {
                    let matches = e
                        .attr("role")
                        .map(|r| r.to_lowercase() == role_lower)
                        .unwrap_or_else(|| implicit_role(&e.tag_name) == role_lower);
                    if matches {
                        result.push(id);
                    }
                }
                for &child in &node.children {
                    queue.push_back(child);
                }
            }
            result
        };
        ids.into_iter().map(|id| self.make_element(id)).collect()
    }

    /// Find all elements whose visible text content contains `text` (case-insensitive).
    pub fn query_text(&self, text: &str) -> Vec<Element> {
        let text_lower = text.to_lowercase();
        let ids = {
            let Ok(doc) = self.lock_doc() else {
                return Vec::new();
            };
            let mut result = Vec::new();
            let mut queue: VecDeque<NodeId> = VecDeque::new();
            queue.push_back(DOCUMENT_NODE_ID);
            while let Some(id) = queue.pop_front() {
                let node = &doc.nodes[id];
                if node.is_element() {
                    let content = doc.text_content(id);
                    if content.to_lowercase().contains(&text_lower) {
                        result.push(id);
                    }
                }
                for &child in &node.children {
                    queue.push_back(child);
                }
            }
            result
        };
        ids.into_iter().map(|id| self.make_element(id)).collect()
    }

    /// Find an `<input>`, `<textarea>`, or `<select>` by `name`, `id`,
    /// or `placeholder` attribute (case-insensitive substring match for placeholder).
    pub fn query_input(&self, name: &str) -> Option<Element> {
        let name_lower = name.to_lowercase();
        let found = {
            let doc = self.lock_doc().ok()?;
            let mut result: Option<NodeId> = None;
            let mut queue: VecDeque<NodeId> = VecDeque::new();
            queue.push_back(DOCUMENT_NODE_ID);
            'outer: while let Some(id) = queue.pop_front() {
                let node = &doc.nodes[id];
                if let NodeData::Element(e) = &node.data {
                    if matches!(e.tag_name.as_str(), "input" | "textarea" | "select") {
                        let by_name = e
                            .attr("name")
                            .map(|v| v.to_lowercase() == name_lower)
                            .unwrap_or(false);
                        let by_id = e
                            .attr("id")
                            .map(|v| v.to_lowercase() == name_lower)
                            .unwrap_or(false);
                        let by_placeholder = e
                            .attr("placeholder")
                            .map(|v| v.to_lowercase().contains(&name_lower))
                            .unwrap_or(false);
                        if by_name || by_id || by_placeholder {
                            result = Some(id);
                            break 'outer;
                        }
                    }
                }
                for &child in &node.children {
                    queue.push_back(child);
                }
            }
            result
        };
        found.map(|id| self.make_element(id))
    }

    /// Find an `<a>` whose visible text or `href` contains `text` (case-insensitive).
    pub fn query_link(&self, text: &str) -> Option<Element> {
        let text_lower = text.to_lowercase();
        let found = {
            let doc = self.lock_doc().ok()?;
            let mut result: Option<NodeId> = None;
            let mut queue: VecDeque<NodeId> = VecDeque::new();
            queue.push_back(DOCUMENT_NODE_ID);
            'outer: while let Some(id) = queue.pop_front() {
                let node = &doc.nodes[id];
                if let NodeData::Element(e) = &node.data {
                    if e.tag_name == "a" {
                        let link_text = doc.text_content(id);
                        let text_match = link_text.to_lowercase().contains(&text_lower);
                        let href_match = e
                            .attr("href")
                            .map(|h| h.to_lowercase().contains(&text_lower))
                            .unwrap_or(false);
                        if text_match || href_match {
                            result = Some(id);
                            break 'outer;
                        }
                    }
                }
                for &child in &node.children {
                    queue.push_back(child);
                }
            }
            result
        };
        found.map(|id| self.make_element(id))
    }
}

/// Map an HTML tag name to its implicit ARIA role string.
fn implicit_role(tag: &str) -> &'static str {
    match tag {
        "a" | "link" => "link",
        "button" => "button",
        "input" => "textbox",
        "select" => "listbox",
        "textarea" => "textbox",
        "form" => "form",
        "nav" => "navigation",
        "main" => "main",
        "header" => "banner",
        "footer" => "contentinfo",
        "aside" => "complementary",
        "section" => "region",
        "article" => "article",
        "h1" | "h2" | "h3" | "h4" | "h5" | "h6" => "heading",
        "img" => "img",
        "table" => "table",
        "tr" => "row",
        "td" => "cell",
        "th" => "columnheader",
        "ul" | "ol" => "list",
        "li" => "listitem",
        "dialog" => "dialog",
        "summary" => "button",
        _ => "",
    }
}
