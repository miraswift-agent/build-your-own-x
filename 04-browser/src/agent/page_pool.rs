//! PagePool — manages multiple pages within a session.

use std::collections::HashMap;

use uuid::Uuid;

use crate::agent::error::BrowserError;
use crate::agent::page::Page;
use crate::agent::resource::{ResourceLimits, ResourceMonitor};

/// Lifecycle state of a managed page.
#[derive(Debug, Clone, PartialEq)]
pub enum PageState {
    Loading,
    Loaded,
    Error(String),
    Closed,
}

/// A page tracked by the pool with its associated metadata.
pub struct PageEntry {
    pub id: String,
    pub page: Page,
    pub state: PageState,
    pub url: Option<String>,
    pub estimated_memory_mb: usize,
}

/// Manages a set of pages within a single session.
pub struct PagePool {
    pages: HashMap<String, PageEntry>,
    monitor: ResourceMonitor,
}

impl PagePool {
    pub fn new(limits: ResourceLimits) -> Self {
        PagePool {
            pages: HashMap::new(),
            monitor: ResourceMonitor::new(limits),
        }
    }

    /// Create a new empty page. Returns its ID.
    pub fn create_page(&mut self) -> Result<String, BrowserError> {
        self.monitor.check_page_limit()?;
        let estimated_mb = ResourceMonitor::estimate_page_memory_mb(0, 0);
        self.monitor.check_memory_limit()?;
        let id = Uuid::new_v4().to_string();
        let entry = PageEntry {
            id: id.clone(),
            page: Page::new(),
            state: PageState::Loaded,
            url: None,
            estimated_memory_mb: estimated_mb,
        };
        self.monitor.on_page_opened(estimated_mb);
        self.pages.insert(id.clone(), entry);
        Ok(id)
    }

    /// Parse `html` into a new page and add it to the pool. Returns the page ID.
    pub fn load_page_from_html(&mut self, html: &str) -> Result<String, BrowserError> {
        self.monitor.check_page_limit()?;
        let estimated_mb = ResourceMonitor::estimate_page_memory_mb(0, html.len());
        self.monitor.check_memory_limit()?;
        let id = Uuid::new_v4().to_string();
        let entry = PageEntry {
            id: id.clone(),
            page: Page::from_html(html),
            state: PageState::Loaded,
            url: None,
            estimated_memory_mb: estimated_mb,
        };
        self.monitor.on_page_opened(estimated_mb);
        self.pages.insert(id.clone(), entry);
        Ok(id)
    }

    /// Add an already-constructed page to the pool. Returns the page ID.
    pub fn add_page(&mut self, page: Page) -> Result<String, BrowserError> {
        self.monitor.check_page_limit()?;
        let estimated_mb = ResourceMonitor::estimate_page_memory_mb(0, 0);
        self.monitor.check_memory_limit()?;
        let id = Uuid::new_v4().to_string();
        let url = page.url().map(|s| s.to_string());
        let entry = PageEntry {
            id: id.clone(),
            page,
            state: PageState::Loaded,
            url,
            estimated_memory_mb: estimated_mb,
        };
        self.monitor.on_page_opened(estimated_mb);
        self.pages.insert(id.clone(), entry);
        Ok(id)
    }

    /// Mark a page as closed and release its tracked resources.
    pub fn close_page(&mut self, page_id: &str) -> bool {
        if let Some(entry) = self.pages.get_mut(page_id) {
            if entry.state == PageState::Closed {
                return false;
            }
            let mb = entry.estimated_memory_mb;
            entry.state = PageState::Closed;
            self.monitor.on_page_closed(mb);
            true
        } else {
            false
        }
    }

    /// Get a reference to a page (returns None if closed or unknown).
    pub fn get_page(&self, page_id: &str) -> Option<&Page> {
        self.pages
            .get(page_id)
            .filter(|e| e.state != PageState::Closed)
            .map(|e| &e.page)
    }

    /// Get a mutable reference to a page.
    pub fn get_page_mut(&mut self, page_id: &str) -> Option<&mut Page> {
        self.pages
            .get_mut(page_id)
            .filter(|e| e.state != PageState::Closed)
            .map(|e| &mut e.page)
    }

    pub fn set_page_state(&mut self, page_id: &str, state: PageState) {
        if let Some(entry) = self.pages.get_mut(page_id) {
            entry.state = state;
        }
    }

    pub fn get_page_state(&self, page_id: &str) -> Option<&PageState> {
        self.pages.get(page_id).map(|e| &e.state)
    }

    pub fn update_page_url(&mut self, page_id: &str, url: String) {
        if let Some(entry) = self.pages.get_mut(page_id) {
            entry.url = Some(url);
        }
    }

    /// IDs of all non-closed pages.
    pub fn page_ids(&self) -> Vec<String> {
        self.pages
            .iter()
            .filter(|(_, e)| e.state != PageState::Closed)
            .map(|(id, _)| id.clone())
            .collect()
    }

    pub fn active_page_count(&self) -> usize {
        self.pages
            .values()
            .filter(|e| e.state != PageState::Closed)
            .count()
    }

    pub fn monitor(&self) -> &ResourceMonitor {
        &self.monitor
    }
}
