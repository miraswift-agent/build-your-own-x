//! Resource limits and monitoring for Stage 06.

use std::time::Duration;

use crate::agent::error::BrowserError;

/// Hard limits applied per-session or globally.
#[derive(Debug, Clone)]
pub struct ResourceLimits {
    pub max_pages: usize,
    pub max_memory_mb: usize,
    pub max_concurrent_requests: usize,
    pub request_timeout: Duration,
}

impl Default for ResourceLimits {
    fn default() -> Self {
        ResourceLimits {
            max_pages: 10,
            max_memory_mb: 512,
            max_concurrent_requests: 50,
            request_timeout: Duration::from_secs(30),
        }
    }
}

impl ResourceLimits {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn with_max_pages(mut self, max: usize) -> Self {
        self.max_pages = max;
        self
    }

    pub fn with_max_memory_mb(mut self, mb: usize) -> Self {
        self.max_memory_mb = mb;
        self
    }

    pub fn with_max_concurrent_requests(mut self, n: usize) -> Self {
        self.max_concurrent_requests = n;
        self
    }

    pub fn with_request_timeout(mut self, d: Duration) -> Self {
        self.request_timeout = d;
        self
    }
}

/// Tracks live resource usage and enforces configured limits.
#[derive(Debug)]
pub struct ResourceMonitor {
    pub limits: ResourceLimits,
    pub current_pages: usize,
    pub current_memory_mb: usize,
    pub current_requests: usize,
}

impl ResourceMonitor {
    pub fn new(limits: ResourceLimits) -> Self {
        ResourceMonitor {
            limits,
            current_pages: 0,
            current_memory_mb: 0,
            current_requests: 0,
        }
    }

    /// Rough memory estimate: ~500 bytes per DOM node + text + 2 MB JS context overhead.
    pub fn estimate_page_memory_mb(dom_nodes: usize, text_bytes: usize) -> usize {
        let bytes = dom_nodes * 500 + text_bytes + 2 * 1024 * 1024;
        (bytes / (1024 * 1024)).max(1)
    }

    pub fn check_page_limit(&self) -> Result<(), BrowserError> {
        if self.current_pages >= self.limits.max_pages {
            return Err(BrowserError::ResourceLimitError {
                message: format!(
                    "max pages exceeded: {} of {} in use",
                    self.current_pages, self.limits.max_pages
                ),
                limit_type: "max_pages".to_string(),
            });
        }
        Ok(())
    }

    pub fn check_memory_limit(&self) -> Result<(), BrowserError> {
        if self.current_memory_mb >= self.limits.max_memory_mb {
            return Err(BrowserError::ResourceLimitError {
                message: format!(
                    "memory limit exceeded: {}MB of {}MB used",
                    self.current_memory_mb, self.limits.max_memory_mb
                ),
                limit_type: "max_memory_mb".to_string(),
            });
        }
        Ok(())
    }

    pub fn on_page_opened(&mut self, estimated_mb: usize) {
        self.current_pages += 1;
        self.current_memory_mb += estimated_mb;
    }

    pub fn on_page_closed(&mut self, estimated_mb: usize) {
        self.current_pages = self.current_pages.saturating_sub(1);
        self.current_memory_mb = self.current_memory_mb.saturating_sub(estimated_mb);
    }

    pub fn memory_usage_percent(&self) -> f64 {
        if self.limits.max_memory_mb == 0 {
            return 0.0;
        }
        (self.current_memory_mb as f64 / self.limits.max_memory_mb as f64) * 100.0
    }

    pub fn is_approaching_memory_limit(&self) -> bool {
        self.memory_usage_percent() >= 80.0
    }

    pub fn is_approaching_page_limit(&self) -> bool {
        self.current_pages + 2 >= self.limits.max_pages
    }
}
