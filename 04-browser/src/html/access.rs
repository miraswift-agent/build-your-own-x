//! Accessibility tree extraction.
//!
//! Derives an accessibility tree from the parsed DOM. The accessibility tree
//! exposes only the information an assistive technology (or an AI agent) needs:
//!
//! * **Role** — what kind of thing this is (button, link, heading, form, ...)
//! * **Name** — the human-readable label (from `aria-label`, `alt`, element text, ...)
//! * **Value** — current state for form controls (placeholder, type, etc.)
//! * **Children** — nested accessible nodes
//!
//! This closely mirrors the WAI-ARIA "accessible name computation" and the
//! Chromium DevTools `AXTree` API — both of which agents interact with.

use std::fmt;

use crate::html::dom::{Document, FormKind, NodeData, NodeId, SemanticRole, DOCUMENT_NODE_ID};

// ---------------------------------------------------------------------------
// Accessible role
// ---------------------------------------------------------------------------

/// The ARIA-compatible role assigned to an accessible node.
#[derive(Debug, Clone, PartialEq)]
pub enum AXRole {
    Document,
    Landmark(LandmarkRole),
    Heading { level: u8 },
    Link,
    Button,
    TextInput,
    Select,
    Textarea,
    Checkbox,
    Radio,
    Slider,
    Form,
    Label,
    List,
    ListItem,
    Table,
    Row,
    Cell,
    ColumnHeader,
    RowHeader,
    Image,
    Figure,
    Article,
    Section,
    Navigation,
    Main,
    Header,
    Footer,
    Aside,
    Paragraph,
    Code,
    Blockquote,
    Generic,
    StaticText,
    None,
}

/// Landmark roles that map to HTML sectioning elements.
#[derive(Debug, Clone, PartialEq)]
pub enum LandmarkRole {
    Banner,      // header (top-level)
    Navigation,  // nav
    Main,        // main
    Complementary, // aside
    ContentInfo, // footer (top-level)
    Search,      // search, form[role=search]
    Form,        // form (with accessible name)
    Region,      // section (with accessible name)
}

// ---------------------------------------------------------------------------
// Accessible node
// ---------------------------------------------------------------------------

/// A node in the accessibility tree.
#[derive(Debug, Clone)]
pub struct AXNode {
    /// The DOM node this corresponds to.
    pub dom_id: NodeId,
    /// ARIA role.
    pub role: AXRole,
    /// Accessible name (empty string if none).
    pub name: String,
    /// Current value (for inputs: value/placeholder, for links: href, ...).
    pub value: Option<String>,
    /// Additional description (aria-description, title, …).
    pub description: Option<String>,
    /// Child accessible nodes.
    pub children: Vec<AXNode>,
}

impl AXNode {
    /// Returns true if this node has any meaningful content for agents.
    pub fn is_meaningful(&self) -> bool {
        !matches!(self.role, AXRole::None | AXRole::Generic)
            || !self.name.is_empty()
            || !self.children.is_empty()
    }
}

// ---------------------------------------------------------------------------
// Accessibility tree
// ---------------------------------------------------------------------------

/// The accessibility tree for a parsed document.
pub struct AccessTree {
    pub root: AXNode,
    /// All headings, in document order (for outline extraction).
    pub headings: Vec<(u8, String)>,
    /// All links, in document order.
    pub links: Vec<(String, String)>, // (text, href)
    /// All form controls, in document order.
    pub forms: Vec<FormSummary>,
    /// All landmark regions.
    pub landmarks: Vec<(LandmarkRole, String)>, // (role, name)
}

/// Summary of a form and its controls.
#[derive(Debug, Clone)]
pub struct FormSummary {
    pub action: Option<String>,
    pub method: Option<String>,
    pub controls: Vec<FormControl>,
}

/// A single form control.
#[derive(Debug, Clone)]
pub struct FormControl {
    pub kind: String,
    pub name: Option<String>,
    pub label: Option<String>,
    pub input_type: Option<String>,
    pub placeholder: Option<String>,
    pub required: bool,
}

// ---------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------

/// Build an `AccessTree` from a parsed `Document`.
pub fn build_access_tree(doc: &Document) -> AccessTree {
    let root_ax = build_node(doc, DOCUMENT_NODE_ID);

    let mut headings = vec![];
    let mut links = vec![];
    let mut forms = vec![];
    let mut landmarks = vec![];

    collect_info(doc, DOCUMENT_NODE_ID, &mut headings, &mut links, &mut forms, &mut landmarks);

    AccessTree { root: root_ax, headings, links, forms, landmarks }
}

fn build_node(doc: &Document, id: NodeId) -> AXNode {
    let node = doc.node(id);
    match &node.data {
        NodeData::Document => {
            let children = node.children.iter()
                .filter_map(|&c| {
                    let n = build_node(doc, c);
                    if n.is_meaningful() { Some(n) } else { None }
                })
                .collect();
            AXNode {
                dom_id: id,
                role: AXRole::Document,
                name: String::new(),
                value: None,
                description: None,
                children,
            }
        }
        NodeData::Text(t) => {
            let trimmed = t.trim().to_string();
            if trimmed.is_empty() {
                AXNode {
                    dom_id: id, role: AXRole::None, name: String::new(),
                    value: None, description: None, children: vec![],
                }
            } else {
                AXNode {
                    dom_id: id, role: AXRole::StaticText, name: trimmed,
                    value: None, description: None, children: vec![],
                }
            }
        }
        NodeData::Comment(_) | NodeData::Doctype(_) => AXNode {
            dom_id: id, role: AXRole::None, name: String::new(),
            value: None, description: None, children: vec![],
        },
        NodeData::Element(e) => {
            // Skip script and style entirely
            if matches!(e.role, SemanticRole::Script { .. }) {
                return AXNode {
                    dom_id: id, role: AXRole::None, name: String::new(),
                    value: None, description: None, children: vec![],
                };
            }

            let role = compute_role(doc, id);
            let name = compute_name(doc, id);
            let value = compute_value(doc, id);
            let description = e.attr("aria-description")
                .or_else(|| e.attr("title"))
                .map(str::to_string);

            // Build children for non-leaf roles
            let children: Vec<AXNode> = node.children.iter()
                .filter_map(|&c| {
                    let n = build_node(doc, c);
                    if n.role != AXRole::None { Some(n) } else { None }
                })
                .collect();

            AXNode { dom_id: id, role, name, value, description, children }
        }
    }
}

// ---------------------------------------------------------------------------
// Role computation
// ---------------------------------------------------------------------------

fn compute_role(doc: &Document, id: NodeId) -> AXRole {
    let node = doc.node(id);
    let e = match node.element_data() { Some(e) => e, None => return AXRole::None };

    // Explicit ARIA role overrides
    if let Some(aria_role) = e.attr("aria-role").or_else(|| e.attr("role")) {
        match aria_role {
            "button" => return AXRole::Button,
            "link" => return AXRole::Link,
            "heading" => return AXRole::Heading { level: 2 },
            "checkbox" => return AXRole::Checkbox,
            "radio" => return AXRole::Radio,
            "textbox" => return AXRole::TextInput,
            "combobox" => return AXRole::Select,
            "listbox" => return AXRole::Select,
            "navigation" => return AXRole::Landmark(LandmarkRole::Navigation),
            "main" => return AXRole::Landmark(LandmarkRole::Main),
            "banner" => return AXRole::Landmark(LandmarkRole::Banner),
            "complementary" => return AXRole::Landmark(LandmarkRole::Complementary),
            "contentinfo" => return AXRole::Landmark(LandmarkRole::ContentInfo),
            "search" => return AXRole::Landmark(LandmarkRole::Search),
            "form" => return AXRole::Landmark(LandmarkRole::Form),
            "region" => return AXRole::Landmark(LandmarkRole::Region),
            "none" | "presentation" => return AXRole::None,
            _ => {}
        }
    }

    // Native element roles
    match &e.role {
        SemanticRole::Heading { level } => AXRole::Heading { level: *level },
        SemanticRole::Link { .. } => match e.tag_name.as_str() {
            "a" => AXRole::Link,
            _ => AXRole::None,
        },
        SemanticRole::Form(kind) => match kind {
            FormKind::Form     => AXRole::Form,
            FormKind::Button   => AXRole::Button,
            FormKind::Select   => AXRole::Select,
            FormKind::Textarea => AXRole::Textarea,
            FormKind::Input    => input_role(e.attr("type").unwrap_or("text")),
            FormKind::Label    => AXRole::Label,
            FormKind::Fieldset => AXRole::Generic,
            FormKind::Legend   => AXRole::Generic,
            FormKind::Other    => AXRole::None,
        },
        SemanticRole::Media { .. } => match e.tag_name.as_str() {
            "img" => AXRole::Image,
            "figure" => AXRole::Figure,
            _ => AXRole::Generic,
        },
        SemanticRole::List => match e.tag_name.as_str() {
            "ul" | "ol" | "dl" | "menu" => AXRole::List,
            "li" | "dt" | "dd" => AXRole::ListItem,
            _ => AXRole::None,
        },
        SemanticRole::Table => match e.tag_name.as_str() {
            "table" => AXRole::Table,
            "tr" => AXRole::Row,
            "td" => AXRole::Cell,
            "th" => {
                let scope = e.attr("scope").unwrap_or("col");
                if scope == "row" { AXRole::RowHeader } else { AXRole::ColumnHeader }
            }
            _ => AXRole::None,
        },
        SemanticRole::Structural => match e.tag_name.as_str() {
            "nav"     => AXRole::Landmark(LandmarkRole::Navigation),
            "main"    => AXRole::Landmark(LandmarkRole::Main),
            "aside"   => AXRole::Landmark(LandmarkRole::Complementary),
            "header"  => {
                // Only landmark if not inside article/section
                AXRole::Landmark(LandmarkRole::Banner)
            }
            "footer"  => AXRole::Landmark(LandmarkRole::ContentInfo),
            "section" => AXRole::Section,
            "article" => AXRole::Article,
            _ => AXRole::Generic,
        },
        SemanticRole::TextFlow => match e.tag_name.as_str() {
            "p"          => AXRole::Paragraph,
            "blockquote" => AXRole::Blockquote,
            "code" | "pre" | "kbd" | "samp" | "var" => AXRole::Code,
            _ => AXRole::Generic,
        },
        SemanticRole::Script { .. } => AXRole::None,
        SemanticRole::Generic => AXRole::Generic,
    }
}

fn input_role(input_type: &str) -> AXRole {
    match input_type {
        "checkbox" => AXRole::Checkbox,
        "radio"    => AXRole::Radio,
        "range"    => AXRole::Slider,
        "submit" | "button" | "image" | "reset" => AXRole::Button,
        "hidden"   => AXRole::None,
        _          => AXRole::TextInput,
    }
}

// ---------------------------------------------------------------------------
// Name computation (accessible name calculation, ARIA spec §4.3)
// ---------------------------------------------------------------------------

fn compute_name(doc: &Document, id: NodeId) -> String {
    let node = doc.node(id);
    let e = match node.element_data() { Some(e) => e, None => return String::new() };

    // 1. aria-labelledby → collect text of referenced elements (skip — no ids map)
    // 2. aria-label
    if let Some(label) = e.attr("aria-label") {
        return label.trim().to_string();
    }

    // 3. Native name sources
    match e.tag_name.as_str() {
        "img" => {
            return e.attr("alt").unwrap_or("").trim().to_string();
        }
        "input" => {
            // Try value for submit/button inputs
            let t = e.attr("type").unwrap_or("text");
            if matches!(t, "submit" | "button" | "reset") {
                if let Some(v) = e.attr("value") { return v.trim().to_string(); }
            }
            // Label text via id (simplified: skip — would need a label → input mapping)
            // Fall back to placeholder
            return e.attr("placeholder").unwrap_or("").trim().to_string();
        }
        "a" => {
            // Text content of the anchor
            let text = doc.text_content(id);
            let trimmed = text.trim().to_string();
            if !trimmed.is_empty() { return trimmed; }
            // Fall back to title
            return e.attr("title").unwrap_or("").trim().to_string();
        }
        "button" => {
            let text = doc.text_content(id);
            let trimmed = text.trim().to_string();
            if !trimmed.is_empty() { return trimmed; }
            return e.attr("value").unwrap_or("").trim().to_string();
        }
        _ => {}
    }

    // For headings: text content
    if matches!(e.role, SemanticRole::Heading { .. }) {
        return doc.text_content(id).trim().to_string();
    }

    // For form elements: name attribute or placeholder
    if matches!(&e.role, SemanticRole::Form(_)) {
        let from_aria = e.attr("aria-label").unwrap_or("");
        if !from_aria.is_empty() { return from_aria.trim().to_string(); }
        let from_placeholder = e.attr("placeholder").unwrap_or("");
        if !from_placeholder.is_empty() { return from_placeholder.trim().to_string(); }
        return e.attr("name").unwrap_or("").trim().to_string();
    }

    // 4. title attribute
    if let Some(title) = e.attr("title") {
        return title.trim().to_string();
    }

    String::new()
}

// ---------------------------------------------------------------------------
// Value computation
// ---------------------------------------------------------------------------

fn compute_value(doc: &Document, id: NodeId) -> Option<String> {
    let e = doc.node(id).element_data()?;
    match e.tag_name.as_str() {
        "a" | "link" => e.attr("href").map(str::to_string),
        "img" | "video" | "audio" | "source" => e.attr("src").map(str::to_string),
        "input" => {
            // type, name, placeholder, value
            let parts: Vec<String> = [
                e.attr("type").map(|v| format!("type={v}")),
                e.attr("name").map(|v| format!("name={v}")),
                e.attr("placeholder").map(|v| format!("placeholder={v}")),
                e.attr("value").map(|v| format!("value={v}")),
                if e.attr("required").is_some() { Some("required".into()) } else { None },
            ].into_iter().flatten().collect();
            if parts.is_empty() { None } else { Some(parts.join(", ")) }
        }
        "select" | "textarea" => {
            e.attr("name").map(|n| format!("name={n}"))
        }
        _ => None,
    }
}

// ---------------------------------------------------------------------------
// Information collectors (for the flat summary lists)
// ---------------------------------------------------------------------------

fn collect_info(
    doc: &Document,
    id: NodeId,
    headings: &mut Vec<(u8, String)>,
    links: &mut Vec<(String, String)>,
    forms: &mut Vec<FormSummary>,
    landmarks: &mut Vec<(LandmarkRole, String)>,
) {
    let node = doc.node(id);
    match &node.data {
        NodeData::Element(e) => {
            // Skip scripts entirely
            if matches!(e.role, SemanticRole::Script { .. }) { return; }

            match &e.role {
                SemanticRole::Heading { level } => {
                    let text = doc.text_content(id).trim().to_string();
                    if !text.is_empty() {
                        headings.push((*level, text));
                    }
                }
                SemanticRole::Link { href } => {
                    if e.tag_name == "a" {
                        let text = doc.text_content(id).trim().to_string();
                        let href_str = href.clone().unwrap_or_default();
                        if !text.is_empty() || !href_str.is_empty() {
                            links.push((text, href_str));
                        }
                    }
                }
                SemanticRole::Form(FormKind::Form) => {
                    let action = e.attr("action").map(str::to_string);
                    let method = e.attr("method").map(str::to_string);
                    let mut controls = vec![];
                    collect_form_controls(doc, id, &mut controls);
                    forms.push(FormSummary { action, method, controls });
                }
                SemanticRole::Structural => {
                    let landmark = match e.tag_name.as_str() {
                        "nav"    => Some(LandmarkRole::Navigation),
                        "main"   => Some(LandmarkRole::Main),
                        "aside"  => Some(LandmarkRole::Complementary),
                        "header" => Some(LandmarkRole::Banner),
                        "footer" => Some(LandmarkRole::ContentInfo),
                        _ => None,
                    };
                    if let Some(lm) = landmark {
                        let name = e.attr("aria-label").unwrap_or("").to_string();
                        landmarks.push((lm, name));
                    }
                }
                _ => {}
            }

            // Recurse
            let children: Vec<NodeId> = node.children.clone();
            for c in children {
                collect_info(doc, c, headings, links, forms, landmarks);
            }
        }
        NodeData::Document => {
            let children: Vec<NodeId> = node.children.clone();
            for c in children {
                collect_info(doc, c, headings, links, forms, landmarks);
            }
        }
        _ => {}
    }
}

fn collect_form_controls(doc: &Document, id: NodeId, controls: &mut Vec<FormControl>) {
    let node = doc.node(id);
    match &node.data {
        NodeData::Element(e) => {
            let is_control = matches!(&e.role,
                SemanticRole::Form(FormKind::Input)
                | SemanticRole::Form(FormKind::Select)
                | SemanticRole::Form(FormKind::Textarea)
                | SemanticRole::Form(FormKind::Button)
            );
            if is_control {
                controls.push(FormControl {
                    kind: e.tag_name.clone(),
                    name: e.attr("name").map(str::to_string),
                    label: e.attr("aria-label")
                        .or_else(|| e.attr("placeholder"))
                        .map(str::to_string),
                    input_type: e.attr("type").map(str::to_string),
                    placeholder: e.attr("placeholder").map(str::to_string),
                    required: e.attr("required").is_some(),
                });
            }
            let children: Vec<NodeId> = node.children.clone();
            for c in children {
                collect_form_controls(doc, c, controls);
            }
        }
        _ => {}
    }
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

impl fmt::Display for AccessTree {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "Accessibility Tree")?;
        writeln!(f, "==================")?;

        if !self.landmarks.is_empty() {
            writeln!(f, "\nLandmarks:")?;
            for (role, name) in &self.landmarks {
                let label = if name.is_empty() { "(unnamed)".to_string() } else { name.clone() };
                writeln!(f, "  [{role:?}] {label}")?;
            }
        }

        if !self.headings.is_empty() {
            writeln!(f, "\nHeadings:")?;
            for (level, text) in &self.headings {
                let indent = "  ".repeat(*level as usize - 1);
                writeln!(f, "  {indent}h{level}: {text}")?;
            }
        }

        if !self.links.is_empty() {
            writeln!(f, "\nLinks:")?;
            for (text, href) in &self.links {
                if href.is_empty() {
                    writeln!(f, "  [link] {text}")?;
                } else {
                    writeln!(f, "  [link] {text} → {href}")?;
                }
            }
        }

        if !self.forms.is_empty() {
            writeln!(f, "\nForms:")?;
            for form in &self.forms {
                let action = form.action.as_deref().unwrap_or("(no action)");
                let method = form.method.as_deref().unwrap_or("get").to_uppercase();
                writeln!(f, "  [form] {method} {action}")?;
                for ctrl in &form.controls {
                    let label = ctrl.label.as_deref().unwrap_or("");
                    let name = ctrl.name.as_deref().unwrap_or("?");
                    let kind = ctrl.input_type.as_deref().unwrap_or(&ctrl.kind);
                    let req = if ctrl.required { " *" } else { "" };
                    writeln!(f, "    [{kind}] name={name} \"{label}\"{req}")?;
                }
            }
        }

        Ok(())
    }
}
