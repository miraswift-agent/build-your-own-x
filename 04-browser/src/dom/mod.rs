//! DOM module — Stage 02: selector matching, mutation observers, enhanced accessibility.
//!
//! ## Modules
//!
//! - `selectors` — CSS selector parsing and matching (no layout needed)
//! - `mutation`  — MutationObserver for detecting DOM changes
//! - `access`    — Enhanced accessibility tree with interactive element catalog

pub mod access;
pub mod mutation;
pub mod selectors;

// Selector API
pub use selectors::{
    matches_selector_list, parse_selector, query_selector, query_selector_all,
    query_selector_all_with, query_selector_with, AttrOp, AttrSelector, Combinator,
    ComplexSelector, CompoundSelector, NthArg, PseudoClass, SelectorError, SelectorList,
    SimpleSelector,
};

// Mutation observer API
pub use mutation::{MutationInit, MutationObserver};

// Enhanced accessibility API
pub use access::{
    build_enhanced_access_tree, compute_accessible_description, compute_accessible_name,
    is_focusable_element, AXNode, AXRole, EnhancedAccessTree, FormControl, FormSummary,
    InteractiveElement, InteractiveKind, LandmarkRole,
};
