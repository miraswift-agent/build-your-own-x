//! Mutation observer — detects DOM changes without polling.
//!
//! # Design
//!
//! DOM mutation methods on `Document` (e.g. `set_attribute`, `insert_child`)
//! append `MutationRecord`s to `doc.pending_mutations`.  A `MutationObserver`
//! holds configuration for which node/types to watch; calling `take_records`
//! drains the matching records from the document queue.
//!
//! This mirrors the browser's MutationObserver API:
//! <https://developer.mozilla.org/en-US/docs/Web/API/MutationObserver>

use crate::html::dom::{Document, MutationKind, MutationRecord, NodeId};

// ─── Configuration ────────────────────────────────────────────────────────────

/// Options for `MutationObserver::new`.
#[derive(Debug, Clone)]
pub struct MutationInit {
    /// Observe child-list additions/removals.
    pub child_list: bool,
    /// Observe attribute changes.
    pub attributes: bool,
    /// Observe text-node content changes.
    pub character_data: bool,
    /// Also observe descendants (not just the direct target).
    pub subtree: bool,
    /// Record the previous attribute value in the mutation record.
    pub attribute_old_value: bool,
    /// Record the previous text content in the mutation record.
    pub character_data_old_value: bool,
    /// If non-empty, only observe these attribute names.
    pub attribute_filter: Vec<String>,
}

impl Default for MutationInit {
    fn default() -> Self {
        MutationInit {
            child_list: false,
            attributes: false,
            character_data: false,
            subtree: false,
            attribute_old_value: false,
            character_data_old_value: false,
            attribute_filter: vec![],
        }
    }
}

impl MutationInit {
    /// Convenience: observe child-list changes.
    pub fn child_list() -> Self {
        MutationInit { child_list: true, ..Default::default() }
    }

    /// Convenience: observe attribute changes.
    pub fn attributes() -> Self {
        MutationInit { attributes: true, ..Default::default() }
    }

    /// Convenience: observe all mutation types on the target and its subtree.
    pub fn all_subtree() -> Self {
        MutationInit {
            child_list: true,
            attributes: true,
            character_data: true,
            subtree: true,
            attribute_old_value: true,
            character_data_old_value: true,
            ..Default::default()
        }
    }
}

// ─── Observer ─────────────────────────────────────────────────────────────────

/// Watches a DOM node for changes and accumulates `MutationRecord`s.
///
/// # Usage
///
/// ```ignore
/// let mut obs = MutationObserver::new(node_id, MutationInit::child_list());
/// doc.insert_child(node_id, NodeData::Text("hello".into()));
/// let records = obs.take_records(&mut doc);
/// assert_eq!(records.len(), 1);
/// ```
pub struct MutationObserver {
    target: NodeId,
    init: MutationInit,
    active: bool,
}

impl MutationObserver {
    pub fn new(target: NodeId, init: MutationInit) -> Self {
        MutationObserver { target, init, active: true }
    }

    pub fn target(&self)    -> NodeId { self.target }
    pub fn is_active(&self) -> bool   { self.active }

    /// Drain all pending mutations from `doc` that match this observer's
    /// target and type filter.
    ///
    /// Records that do **not** match are put back into `doc.pending_mutations`
    /// so other observers can still see them.
    pub fn take_records(&self, doc: &mut Document) -> Vec<MutationRecord> {
        if !self.active { return vec![]; }

        let all = std::mem::take(&mut doc.pending_mutations);
        let mut matched   = vec![];
        let mut remaining = vec![];

        for record in all {
            if self.matches(&record, &doc.nodes) {
                matched.push(record);
            } else {
                remaining.push(record);
            }
        }
        doc.pending_mutations = remaining;
        matched
    }

    /// Stop observing; future `take_records` calls return empty.
    pub fn disconnect(&mut self) {
        self.active = false;
    }

    // ── internal ────────────────────────────────────────────────────────────

    fn matches(&self, record: &MutationRecord, nodes: &[crate::html::dom::Node]) -> bool {
        // Check target: direct match or (subtree + descendant).
        let target_ok = record.target == self.target
            || (self.init.subtree && is_descendant(nodes, record.target, self.target));
        if !target_ok { return false; }

        // Check mutation kind.
        match record.kind {
            MutationKind::ChildList => self.init.child_list,
            MutationKind::CharacterData => self.init.character_data,
            MutationKind::Attributes => {
                if !self.init.attributes { return false; }
                if !self.init.attribute_filter.is_empty() {
                    return record.attribute_name.as_ref()
                        .map(|n| self.init.attribute_filter.iter().any(|f| f == n))
                        .unwrap_or(false);
                }
                true
            }
        }
    }
}

/// Returns `true` if `node` is a descendant of `ancestor`.
fn is_descendant(nodes: &[crate::html::dom::Node], node: NodeId, ancestor: NodeId) -> bool {
    let mut cur = nodes[node].parent;
    while let Some(id) = cur {
        if id == ancestor { return true; }
        cur = nodes[id].parent;
    }
    false
}
