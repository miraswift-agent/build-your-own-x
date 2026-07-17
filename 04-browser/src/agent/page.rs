//! Page — the top-level agent interface for a loaded web page.

use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use crate::dom::query_selector;
use crate::html::dom::{Document, NodeData, NodeId, DOCUMENT_NODE_ID};
use crate::html::parse;
use crate::net::{CookieJar, HttpClient, Url};

use super::element::Element;

#[derive(Debug, Clone, PartialEq)]
pub enum LoadState {
    Idle,
    Loading,
    Loaded,
}

pub struct Page {
    pub(crate) doc: Arc<Mutex<Document>>,
    pub(crate) current_url: Option<String>,
    /// Full URL history in navigation order.
    pub(crate) history: Vec<String>,
    /// Index of the current page in `history`.
    pub(crate) history_pos: usize,
    pub(crate) cookies: CookieJar,
    pub(crate) load_state: LoadState,
}

impl Page {
    pub fn new() -> Self {
        Page {
            doc: Arc::new(Mutex::new(Document::new())),
            current_url: None,
            history: Vec::new(),
            history_pos: 0,
            cookies: CookieJar::new(),
            load_state: LoadState::Idle,
        }
    }

    /// Create a Page from a raw HTML string (no network, for tests and offline use).
    pub fn from_html(html: &str) -> Self {
        let doc = parse(html);
        Page {
            doc: Arc::new(Mutex::new(doc)),
            current_url: None,
            history: Vec::new(),
            history_pos: 0,
            cookies: CookieJar::new(),
            load_state: LoadState::Loaded,
        }
    }

    /// Create a Page from HTML with a known URL (sets history).
    pub fn from_html_with_url(html: &str, url: &str) -> Self {
        let doc = parse(html);
        Page {
            doc: Arc::new(Mutex::new(doc)),
            current_url: Some(url.to_string()),
            history: vec![url.to_string()],
            history_pos: 0,
            cookies: CookieJar::new(),
            load_state: LoadState::Loaded,
        }
    }

    /// Fetch `url`, parse the HTML, and replace the current page.
    pub async fn goto(&mut self, url: &str) -> Result<(), String> {
        let parsed = Url::parse(url)?;
        let client = HttpClient::with_defaults();
        self.load_state = LoadState::Loading;
        let response = client.get(&parsed, &mut self.cookies).await?;
        let html = response.body_as_str().to_string();
        let doc = parse(&html);
        let final_url = response.final_url.as_str().to_string();
        *self.doc.lock().unwrap() = doc;
        // Truncate forward history, then push.
        if !self.history.is_empty() {
            self.history.truncate(self.history_pos + 1);
        }
        self.history.push(final_url.clone());
        self.history_pos = self.history.len() - 1;
        self.current_url = Some(final_url);
        self.load_state = LoadState::Loaded;
        Ok(())
    }

    /// Serialise the current DOM as an HTML string.
    pub fn content(&self) -> String {
        let doc = self.doc.lock().unwrap();
        doc.outer_html(DOCUMENT_NODE_ID)
    }

    /// Return the text content of the `<title>` element, or empty string.
    pub fn title(&self) -> String {
        let doc = self.doc.lock().unwrap();
        doc.find_element("title")
            .map(|id| doc.text_content(id))
            .unwrap_or_default()
    }

    /// The current page URL (set by `goto` / `from_html_with_url`).
    pub fn url(&self) -> Option<&str> {
        self.current_url.as_deref()
    }

    /// Re-fetch the current URL and replace the DOM.
    pub async fn reload(&mut self) -> Result<(), String> {
        let url = self.current_url.clone().ok_or("no current URL to reload")?;
        let parsed = Url::parse(&url)?;
        let client = HttpClient::with_defaults();
        let response = client.get(&parsed, &mut self.cookies).await?;
        let html = response.body_as_str().to_string();
        let doc = parse(&html);
        *self.doc.lock().unwrap() = doc;
        self.load_state = LoadState::Loaded;
        Ok(())
    }

    /// Navigate backward in history. Returns `true` if navigation happened.
    pub async fn go_back(&mut self) -> bool {
        if self.history_pos == 0 {
            return false;
        }
        self.history_pos -= 1;
        let url = self.history[self.history_pos].clone();
        self.current_url = Some(url.clone());
        self.reload_url(&url).await;
        true
    }

    /// Navigate forward in history. Returns `true` if navigation happened.
    pub async fn go_forward(&mut self) -> bool {
        if self.history_pos + 1 >= self.history.len() {
            return false;
        }
        self.history_pos += 1;
        let url = self.history[self.history_pos].clone();
        self.current_url = Some(url.clone());
        self.reload_url(&url).await;
        true
    }

    async fn reload_url(&mut self, url: &str) {
        if let Ok(parsed) = Url::parse(url) {
            let client = HttpClient::with_defaults();
            if let Ok(resp) = client.get(&parsed, &mut self.cookies).await {
                let html = resp.body_as_str().to_string();
                let doc = parse(&html);
                *self.doc.lock().unwrap() = doc;
                self.load_state = LoadState::Loaded;
            }
        }
    }

    /// Poll until `selector` matches an element or `timeout` elapses.
    pub fn wait_for_selector(&self, selector: &str, timeout: Duration) -> Result<Element, String> {
        let start = Instant::now();
        loop {
            let found = {
                let doc = self.doc.lock().unwrap();
                query_selector(&doc, DOCUMENT_NODE_ID, selector)
                    .ok()
                    .flatten()
            };
            if let Some(id) = found {
                return Ok(self.make_element(id));
            }
            if start.elapsed() >= timeout {
                return Err(format!("timeout waiting for selector '{selector}'"));
            }
            std::thread::sleep(Duration::from_millis(50));
        }
    }

    /// No-op in our synchronous model — loading is complete after `goto`.
    pub fn wait_for_load_state(&self, _state: &str) {}

    /// Return a human-readable text representation of visible content
    /// (the accessibility tree / text content, not rendered pixels).
    pub fn screenshot(&self) -> String {
        let doc = self.doc.lock().unwrap();
        let mut out = String::new();
        collect_visible_text(&doc, DOCUMENT_NODE_ID, &mut out);
        out
    }

    /// Build an Element handle for the given node id.
    pub(crate) fn make_element(&self, node_id: NodeId) -> Element {
        Element::new(node_id, Arc::clone(&self.doc))
    }
}

impl Default for Page {
    fn default() -> Self {
        Self::new()
    }
}

fn collect_visible_text(doc: &Document, node_id: NodeId, out: &mut String) {
    let mut stack = vec![node_id];
    while let Some(id) = stack.pop() {
        let node = doc.node(id);
        match &node.data {
            NodeData::Text(t) => {
                let trimmed = t.trim();
                if !trimmed.is_empty() {
                    out.push_str(trimmed);
                    out.push('\n');
                }
            }
            NodeData::Element(e) => {
                if matches!(e.tag_name.as_str(), "script" | "style" | "noscript") {
                    continue;
                }
                if let Some(style) = e.attr("style") {
                    let s = style.to_lowercase().replace(' ', "");
                    if s.contains("display:none") || s.contains("visibility:hidden") {
                        continue;
                    }
                }
                if e.attr("hidden").is_some() {
                    continue;
                }
                if let Some(level_char) = e
                    .tag_name
                    .strip_prefix('h')
                    .and_then(|s| s.parse::<usize>().ok())
                {
                    out.push_str(&"#".repeat(level_char));
                    out.push(' ');
                }
                for &child in node.children.iter().rev() {
                    stack.push(child);
                }
            }
            _ => {
                for &child in node.children.iter().rev() {
                    stack.push(child);
                }
            }
        }
    }
}
