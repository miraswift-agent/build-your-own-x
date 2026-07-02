//! HTML parsing — Stage 01 of the agent browser.
//!
//! The entry point is [`parse`] which takes an HTML string and returns a
//! [`Document`]. For accessibility extraction, use [`build_access_tree`].

pub mod access;
pub mod dom;
pub mod tokenizer;
pub mod tree_builder;

// Re-export the full public surface so tests and downstream crates can use
// `use agent_browser::html::Foo` without knowing which submodule owns it.
#[allow(unused_imports)]
pub use dom::{
    Attribute, Document, ElementCategory, ElementData, FormKind, MutationKind, MutationRecord,
    Node, NodeData, NodeId, SemanticRole, DOCUMENT_NODE_ID,
};
#[allow(unused_imports)]
pub use tokenizer::{tokenize, Token};
pub use tree_builder::parse;
#[allow(unused_imports)]
pub use access::{build_access_tree, AccessTree, AXNode, AXRole};
