//! Action API — enum-driven, chainable agent actions.

use std::time::Duration;

use crate::dom::query_selector;
use crate::html::dom::DOCUMENT_NODE_ID;

use super::page::Page;

/// A single agent action.
///
/// Honesty contract (partial-browser semantics):
/// - **Real DOM side effects:** [`Click`], [`Type`], [`Select`] (attribute-level).
/// - **Partial / no viewport model:** [`Scroll`] — logged only; message tagged `stub`.
/// - **Partial / no pointer events:** [`Hover`] — requires a matching element, then
///   logs only; message tagged `stub`.
/// - **Not wired on this path:** [`Evaluate`] — returns `Err` (use CDP/`eval` CLI).
/// - **Real wait:** [`Wait`] sleeps the calling thread.
#[derive(Debug, Clone)]
pub enum Action {
    /// Click the element matching `selector`.
    Click(String),
    /// Type `text` into the element matching `selector`.
    Type(String, String),
    /// Scroll the page by `(x, y)` pixels.
    ///
    /// **Stub:** no viewport/layout model — records intent only.
    Scroll(i32, i32),
    /// Pick `value` in the `<select>` matching `selector`.
    Select(String, String),
    /// Hover over the element matching `selector`.
    ///
    /// **Stub:** verifies the element exists, then logs only (no pointer events,
    /// no `:hover` CSS).
    Hover(String),
    /// Pause execution for `duration`.
    Wait(Duration),
    /// Evaluate a JavaScript expression.
    ///
    /// **Not implemented on `Page::execute`.** Returns `Err` so callers cannot
    /// mistake a log line for a real JS result. Use the CDP/CLI eval path.
    Evaluate(String),
}

/// A sequence of [`Action`]s built with a fluent API.
#[derive(Debug, Default)]
pub struct ActionChain {
    pub(crate) actions: Vec<Action>,
}

impl ActionChain {
    pub fn new() -> Self {
        ActionChain {
            actions: Vec::new(),
        }
    }

    /// Append `action` and return `self` for chaining.
    pub fn and_then(mut self, action: Action) -> Self {
        self.actions.push(action);
        self
    }
}

impl Page {
    /// Execute a single [`Action`] and return a log message describing what happened.
    ///
    /// See [`Action`] for which variants are real vs stub vs not-wired.
    pub fn execute(&mut self, action: Action) -> Result<String, String> {
        match action {
            Action::Click(ref selector) => {
                let node_id = {
                    let doc = self.lock_doc()?;
                    query_selector(&doc, DOCUMENT_NODE_ID, selector)
                        .map_err(|e| format!("selector error: {e}"))?
                };
                match node_id {
                    Some(id) => {
                        let tag = self
                            .lock_doc()?
                            .nodes[id]
                            .tag_name()
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
                    let doc = self.lock_doc()?;
                    query_selector(&doc, DOCUMENT_NODE_ID, selector)
                        .map_err(|e| format!("selector error: {e}"))?
                };
                match node_id {
                    Some(id) => {
                        self.lock_doc_mut()?.set_attribute(id, "value", text);
                        let msg = format!("[type] selector={selector:?} text={text:?}");
                        eprintln!("{msg}");
                        Ok(msg)
                    }
                    None => Err(format!("no element matches selector {selector:?}")),
                }
            }

            Action::Scroll(x, y) => {
                // No viewport/layout engine — admit the stub in the success string.
                let msg = format!(
                    "[scroll:stub] dx={x} dy={y} (no viewport model; intent logged only)"
                );
                eprintln!("{msg}");
                Ok(msg)
            }

            Action::Select(ref selector, ref value) => {
                let node_id = {
                    let doc = self.lock_doc()?;
                    query_selector(&doc, DOCUMENT_NODE_ID, selector)
                        .map_err(|e| format!("selector error: {e}"))?
                };
                match node_id {
                    Some(id) => {
                        self.lock_doc_mut()?.set_attribute(id, "value", value);
                        let msg = format!("[select] selector={selector:?} value={value:?}");
                        eprintln!("{msg}");
                        Ok(msg)
                    }
                    None => Err(format!("no element matches selector {selector:?}")),
                }
            }

            Action::Hover(ref selector) => {
                // Require a match (unlike the old always-Ok path) but still no pointer events.
                let node_id = {
                    let doc = self.lock_doc()?;
                    query_selector(&doc, DOCUMENT_NODE_ID, selector)
                        .map_err(|e| format!("selector error: {e}"))?
                };
                match node_id {
                    Some(id) => {
                        let tag = self
                            .lock_doc()?
                            .nodes[id]
                            .tag_name()
                            .unwrap_or("?")
                            .to_string();
                        let msg = format!(
                            "[hover:stub] <{tag}> selector={selector:?} node_id={id} (no pointer events)"
                        );
                        eprintln!("{msg}");
                        Ok(msg)
                    }
                    None => Err(format!("no element matches selector {selector:?}")),
                }
            }

            Action::Wait(duration) => {
                std::thread::sleep(duration);
                let msg = format!("[wait] {}ms", duration.as_millis());
                Ok(msg)
            }

            Action::Evaluate(ref script) => Err(format!(
                "Action::Evaluate is not wired on Page::execute (script={script:?}); \
                 use CDP Runtime.evaluate or `agent-browser eval` — refusing silent stub success"
            )),
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
