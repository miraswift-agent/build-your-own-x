//! Agent API — Stage 05: high-level interface for agents to interact with web pages.
//!
//! Modules:
//! - `page`    — Page struct: fetch, navigate, query, screenshot
//! - `element` — Element struct: read/write DOM nodes
//! - `query`   — CSS, role, text, input, and link queries
//! - `action`  — Action enum + ActionChain for scripted interaction
//! - `extract` — Data extraction: tables, links, forms, metadata, text

pub mod action;
pub mod element;
pub mod extract;
pub mod page;
pub mod query;

pub use action::{Action, ActionChain};
pub use element::Element;
pub use extract::{FormInfo, InputInfo, PageMetadata};
pub use page::{LoadState, Page};
