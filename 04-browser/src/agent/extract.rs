//! Data extraction — tables, links, forms, metadata, plain text, structured JSON.

use std::collections::VecDeque;

use serde_json::{json, Value};

use crate::html::dom::{Document, NodeData, NodeId, DOCUMENT_NODE_ID};
use crate::dom::query_selector_all;

use super::page::Page;

// ─── Public data types ────────────────────────────────────────────────────────

pub struct FormInfo {
    pub action: Option<String>,
    pub method: String,
    pub inputs: Vec<InputInfo>,
}

pub struct InputInfo {
    pub name: Option<String>,
    pub input_type: String,
    pub value: Option<String>,
    pub placeholder: Option<String>,
    pub required: bool,
}

pub struct PageMetadata {
    pub title: String,
    pub description: Option<String>,
    pub og_title: Option<String>,
    pub og_description: Option<String>,
    pub og_image: Option<String>,
    pub canonical_url: Option<String>,
}

// ─── Page extraction methods ──────────────────────────────────────────────────

impl Page {
    /// Extract rows from all tables matching `selector`.
    /// Each row is a `Vec<String>` of cell text values.
    pub fn extract_table(&self, selector: &str) -> Vec<Vec<String>> {
        let doc = self.doc.lock().unwrap();
        let table_ids =
            query_selector_all(&doc, DOCUMENT_NODE_ID, selector).unwrap_or_default();
        let mut out = Vec::new();
        for table_id in table_ids {
            extract_rows_from_table(&doc, table_id, &mut out);
        }
        out
    }

    /// Return `(text, href)` pairs for every `<a>` element in the page.
    pub fn extract_links(&self) -> Vec<(String, String)> {
        let doc = self.doc.lock().unwrap();
        collect_links(&doc, DOCUMENT_NODE_ID)
    }

    /// Return one [`FormInfo`] per `<form>` element in the page.
    pub fn extract_forms(&self) -> Vec<FormInfo> {
        let doc = self.doc.lock().unwrap();
        doc.find_all_elements("form")
            .into_iter()
            .map(|form_id| {
                let (action, method) = match &doc.nodes[form_id].data {
                    NodeData::Element(e) => (
                        e.attr("action").map(str::to_string),
                        e.attr("method").unwrap_or("get").to_string(),
                    ),
                    _ => (None, "get".to_string()),
                };
                let inputs = collect_inputs(&doc, form_id);
                FormInfo { action, method, inputs }
            })
            .collect()
    }

    /// Return page-level metadata: title, `<meta>` description, Open Graph tags,
    /// and canonical URL.
    pub fn extract_metadata(&self) -> PageMetadata {
        let doc = self.doc.lock().unwrap();
        extract_metadata_from_doc(&doc)
    }

    /// Return all visible text content as a single string.
    pub fn extract_text(&self) -> String {
        let doc = self.doc.lock().unwrap();
        let mut out = String::new();
        collect_text(&doc, DOCUMENT_NODE_ID, &mut out);
        out.trim().to_string()
    }

    /// Return a JSON object with page metadata, links, and (if schema is
    /// `"tables"`) extracted tables. The `schema` parameter is advisory and
    /// currently selects which top-level keys to populate.
    pub fn extract_structured(&self, schema: &str) -> Value {
        let doc = self.doc.lock().unwrap();
        let meta = extract_metadata_from_doc(&doc);
        let links: Vec<Value> = collect_links(&doc, DOCUMENT_NODE_ID)
            .into_iter()
            .map(|(text, href)| json!({ "text": text, "href": href }))
            .collect();

        let mut obj = json!({
            "title": meta.title,
            "description": meta.description,
            "url": self.current_url,
            "links": links,
        });

        if schema == "tables" || schema.is_empty() || schema == "full" {
            let table_ids =
                query_selector_all(&doc, DOCUMENT_NODE_ID, "table").unwrap_or_default();
            let tables: Vec<Value> = table_ids
                .into_iter()
                .map(|tid| {
                    let mut rows = Vec::new();
                    extract_rows_from_table(&doc, tid, &mut rows);
                    Value::Array(
                        rows.into_iter()
                            .map(|row| Value::Array(row.into_iter().map(Value::String).collect()))
                            .collect(),
                    )
                })
                .collect();
            obj["tables"] = Value::Array(tables);
        }

        obj
    }
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

fn extract_metadata_from_doc(doc: &Document) -> PageMetadata {
    let title = doc
        .find_element("title")
        .map(|id| doc.text_content(id))
        .unwrap_or_default();

    let mut description = None;
    let mut og_title = None;
    let mut og_description = None;
    let mut og_image = None;
    let mut canonical_url = None;

    for meta_id in doc.find_all_elements("meta") {
        if let NodeData::Element(e) = &doc.nodes[meta_id].data {
            if let Some(name) = e.attr("name") {
                if name.eq_ignore_ascii_case("description") {
                    description = e.attr("content").map(str::to_string);
                }
            }
            if let Some(prop) = e.attr("property") {
                match prop.to_lowercase().as_str() {
                    "og:title"       => og_title = e.attr("content").map(str::to_string),
                    "og:description" => og_description = e.attr("content").map(str::to_string),
                    "og:image"       => og_image = e.attr("content").map(str::to_string),
                    _ => {}
                }
            }
        }
    }

    for link_id in doc.find_all_elements("link") {
        if let NodeData::Element(e) = &doc.nodes[link_id].data {
            if e.attr("rel").map(|r| r.eq_ignore_ascii_case("canonical")).unwrap_or(false) {
                canonical_url = e.attr("href").map(str::to_string);
            }
        }
    }

    PageMetadata { title, description, og_title, og_description, og_image, canonical_url }
}

/// Extract `(text, href)` links starting from `root`.
fn collect_links(doc: &Document, root: NodeId) -> Vec<(String, String)> {
    let mut links = Vec::new();
    let mut queue: VecDeque<NodeId> = VecDeque::new();
    queue.push_back(root);
    while let Some(id) = queue.pop_front() {
        let node = &doc.nodes[id];
        if let NodeData::Element(e) = &node.data {
            if e.tag_name == "a" {
                let text = doc.text_content(id).trim().to_string();
                let href = e.attr("href").unwrap_or("").to_string();
                links.push((text, href));
            }
        }
        for &child in &node.children {
            queue.push_back(child);
        }
    }
    links
}

/// Append rows from a single `<table>` node into `out`.
fn extract_rows_from_table(doc: &Document, table_id: NodeId, out: &mut Vec<Vec<String>>) {
    // Collect all <tr> descendants using BFS.
    let mut queue: VecDeque<NodeId> = VecDeque::new();
    queue.push_back(table_id);
    while let Some(id) = queue.pop_front() {
        let node = &doc.nodes[id];
        if let NodeData::Element(e) = &node.data {
            if e.tag_name == "tr" {
                let row: Vec<String> = node
                    .children
                    .iter()
                    .filter_map(|&c| {
                        let cn = &doc.nodes[c];
                        cn.element_data()
                            .filter(|e| matches!(e.tag_name.as_str(), "td" | "th"))
                            .map(|_| doc.text_content(c).trim().to_string())
                    })
                    .collect();
                if !row.is_empty() {
                    out.push(row);
                }
                // Don't recurse into nested tables via the outer BFS,
                // but do process children for nested row groups.
            }
        }
        for &child in &node.children {
            queue.push_back(child);
        }
    }
}

/// Collect `<input>`, `<select>`, `<textarea>` elements inside a form.
fn collect_inputs(doc: &Document, form_id: NodeId) -> Vec<InputInfo> {
    let mut inputs = Vec::new();
    let mut queue: VecDeque<NodeId> = VecDeque::new();
    // Start from form's children to avoid re-processing the form itself.
    for &c in &doc.nodes[form_id].children {
        queue.push_back(c);
    }
    while let Some(id) = queue.pop_front() {
        if let NodeData::Element(e) = &doc.nodes[id].data {
            match e.tag_name.as_str() {
                "input" => {
                    inputs.push(InputInfo {
                        name: e.attr("name").map(str::to_string),
                        input_type: e.attr("type").unwrap_or("text").to_string(),
                        value: e.attr("value").map(str::to_string),
                        placeholder: e.attr("placeholder").map(str::to_string),
                        required: e.attr("required").is_some(),
                    });
                }
                "textarea" => {
                    inputs.push(InputInfo {
                        name: e.attr("name").map(str::to_string),
                        input_type: "textarea".to_string(),
                        value: Some(doc.text_content(id)),
                        placeholder: e.attr("placeholder").map(str::to_string),
                        required: e.attr("required").is_some(),
                    });
                }
                "select" => {
                    inputs.push(InputInfo {
                        name: e.attr("name").map(str::to_string),
                        input_type: "select".to_string(),
                        value: e.attr("value").map(str::to_string),
                        placeholder: None,
                        required: e.attr("required").is_some(),
                    });
                }
                _ => {}
            }
        }
        for &child in &doc.nodes[id].children {
            queue.push_back(child);
        }
    }
    inputs
}

/// Recursively collect visible text, skipping `<script>`, `<style>`,
/// `hidden` attribute, and inline `display:none` / `visibility:hidden`.
fn collect_text(doc: &Document, node_id: NodeId, out: &mut String) {
    let node = &doc.nodes[node_id];
    match &node.data {
        NodeData::Text(t) => {
            let trimmed = t.trim();
            if !trimmed.is_empty() {
                if !out.is_empty() && !out.ends_with('\n') {
                    out.push(' ');
                }
                out.push_str(trimmed);
            }
        }
        NodeData::Element(e) => {
            // Skip non-content elements.
            if matches!(e.tag_name.as_str(), "script" | "style" | "noscript") {
                return;
            }
            // Skip hidden elements.
            if e.attr("hidden").is_some() {
                return;
            }
            if let Some(style) = e.attr("style") {
                let s = style.to_lowercase().replace(' ', "");
                if s.contains("display:none") || s.contains("visibility:hidden") {
                    return;
                }
            }
            let children: Vec<NodeId> = node.children.clone();
            for child in children {
                collect_text(doc, child, out);
            }
        }
        _ => {
            let children: Vec<NodeId> = node.children.clone();
            for child in children {
                collect_text(doc, child, out);
            }
        }
    }
}
