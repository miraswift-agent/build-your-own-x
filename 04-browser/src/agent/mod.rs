//! Agent API — Stage 05/06: high-level interface for agents to interact with web pages.
//!
//! Modules:
//! - `page`      — Page struct: fetch, navigate, query, screenshot
//! - `element`   — Element struct: read/write DOM nodes
//! - `query`     — CSS, role, text, input, and link queries
//! - `action`    — Action enum + ActionChain for scripted interaction
//! - `extract`   — Data extraction: tables, links, forms, metadata, text
//! - `error`     — BrowserError, ErrorChain, RetryPolicy, CrashRecovery
//! - `resource`  — ResourceLimits, ResourceMonitor
//! - `page_pool` — PagePool: multiple pages per session
//! - `session`   — Session, SessionManager: multi-session management + persistence

pub mod action;
pub mod element;
pub mod error;
pub mod extract;
pub mod page;
pub mod page_pool;
pub mod query;
pub mod resource;
pub mod session;

pub use action::{Action, ActionChain};
pub use element::Element;
pub use error::{BrowserError, CrashRecovery, ErrorChain, RetryPolicy};
pub use extract::{FormInfo, InputInfo, PageMetadata};
pub use page::{LoadState, Page};
pub use page_pool::{PageEntry, PagePool, PageState};
pub use resource::{ResourceLimits, ResourceMonitor};
pub use session::{Session, SessionData, SessionManager};
