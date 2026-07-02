//! Action API — enum-driven, chainable agent actions.

use std::time::Duration;

use crate::html::dom::DOCUMENT_NODE_ID;
use crate::dom::query_selector;

use super::page::Page;

/// A single agent action.
#[derive(Debug, Clone)]
pub enum Action {
    /// Click the element matching `selector`.
    Click(String),
    /// Type `text` into the element matching `selector`.
    Type(String, String),
    /// Scroll the page by `(x, y)` pixels.
    Scroll(i32, i32),
    /// Pick `value` in the `<select>` matching `selector`.
    Select(String, String),
    /// Hover over the element matching `selector` (logged only).
    Hover(String),
    /// Pause execution for `duration`.
    Wait(Duration),
    /// Evaluate a JavaScript expression (logged; JS bridge not wired here).
    Evaluate(String),
}

/// A sequence of [`Action`]s built with a fluent API.
#[derive(Debug, Default)]
pub struct ActionChain {
    pub(crate) actions: Vec<Action>,
}

impl ActionChain {
    pub fn new() -> Self {
        ActionChain { actions: Vec::new() }
    }

    /// Append `action` and return `self` for chaining.
    pub fn and_then(mut self, action: Action) -> Self {
        self.actions.push(action);
        self
    }
}

impl Page {
    /// Execute a single [`Action`] and return a log message describing what happened.
    pub fn execute(&mut self, action: Action) -> Result<String, String> {
        match action {
            Action::Click(ref selector) => {
                let node_id = {
                    let doc = self.doc.lock().unwrap();
                    query_selector(&doc, DOCUMENT_NODE_ID, selector)
                        .map_err(|e| format!("selector error: {e}"))?
                };
                match node_id {
                    Some(id) => {
                        let tag = self.doc.lock().unwrap()
                            .nodes[id].tag_name()
                            .unwrap_or("?")
                            .to_string();
                        let msg = format!("[click] <{tag}> selector={selector:?} node_id={id}");
                        eprintln!("{msg}");
                        Ok(msg)
                    }
                    None => Err(format!("no element matches selector {selector:?}")),
                }
            }

            Action::Type(ref selector, ref text) => {
                let node_id = {
                    let doc = self.doc.lock().unwrap();
                    query_selector(&doc, DOCUMENT_NODE_ID, selector)
                        .map_err(|e| format!("selector error: {e}"))?
                };
                match node_id {
                    Some(id) => {
                        self.doc.lock().unwrap().set_attribute(id, "value", text);
                        let msg = format!("[type] selector={selector:?} text={text:?}");
                        eprintln!("{msg}");
                        Ok(msg)
                    }
                    None => Err(format!("no element matches selector {selector:?}")),
                }
            }

            Action::Scroll(x, y) => {
                let msg = format!("[scroll] dx={x} dy={y}");
                eprintln!("{msg}");
                Ok(msg)
            }

            Action::Select(ref selector, ref value) => {
                let node_id = {
                    let doc = self.doc.lock().unwrap();
                    query_selector(&doc, DOCUMENT_NODE_ID, selector)
                        .map_err(|e| format!("selector error: {e}"))?
                };
                match node_id {
                    Some(id) => {
                        self.doc.lock().unwrap().set_attribute(id, "value", value);
                        let msg = format!("[select] selector={selector:?} value={value:?}");
                        eprintln!("{msg}");
                        Ok(msg)
                    }
                    None => Err(format!("no element matches selector {selector:?}")),
                }
            }

            Action::Hover(ref selector) => {
                let msg = format!("[hover] selector={selector:?}");
                eprintln!("{msg}");
                Ok(msg)
            }

            Action::Wait(duration) => {
                std::thread::sleep(duration);
                let msg = format!("[wait] {}ms", duration.as_millis());
                Ok(msg)
            }

            Action::Evaluate(ref script) => {
                let msg = format!("[eval] {script:?}");
                eprintln!("{msg}");
                Ok(msg)
            }
        }
    }

    /// Execute every action in an [`ActionChain`] in order.
    /// Stops and returns the first error encountered.
    pub fn execute_chain(&mut self, chain: ActionChain) -> Result<Vec<String>, String> {
        let mut results = Vec::new();
        for action in chain.actions {
            results.push(self.execute(action)?);
        }
        Ok(results)
    }
}
