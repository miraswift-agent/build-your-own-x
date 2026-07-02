//! Session management — multiple isolated browser sessions with persistence.

use std::collections::HashMap;
use std::path::Path;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use serde::{Deserialize, Serialize};
use uuid::Uuid;

use crate::agent::error::{BrowserError, CrashRecovery};
use crate::agent::page::Page;
use crate::agent::page_pool::PagePool;
use crate::agent::resource::ResourceLimits;
use crate::net::cookies::CookieJar;

/// A single isolated browser session: own pages, cookies, JS contexts, history.
pub struct Session {
    pub id: String,
    pub page_pool: PagePool,
    /// Cookies isolated to this session; never shared across sessions.
    pub cookies: CookieJar,
    /// Tracks which pages have crashed JS contexts.
    pub crash_recovery: CrashRecovery,
    pub history: Vec<String>,
    pub created_at: SystemTime,
    pub(crate) last_active: Instant,
    limits: ResourceLimits,
}

impl Session {
    pub fn new(limits: ResourceLimits) -> Self {
        Session {
            id: Uuid::new_v4().to_string(),
            page_pool: PagePool::new(limits.clone()),
            cookies: CookieJar::new(),
            crash_recovery: CrashRecovery::new(),
            history: Vec::new(),
            created_at: SystemTime::now(),
            last_active: Instant::now(),
            limits,
        }
    }

    /// Restore a session from a saved ID (e.g. after loading from disk).
    pub fn with_id(id: String, limits: ResourceLimits) -> Self {
        Session {
            id,
            page_pool: PagePool::new(limits.clone()),
            cookies: CookieJar::new(),
            crash_recovery: CrashRecovery::new(),
            history: Vec::new(),
            created_at: SystemTime::now(),
            last_active: Instant::now(),
            limits,
        }
    }

    /// Mark the session active now (resets timeout counter).
    pub fn touch(&mut self) {
        self.last_active = Instant::now();
    }

    /// Whether the session has been idle longer than `timeout`.
    pub fn is_expired(&self, timeout: Duration) -> bool {
        self.last_active.elapsed() > timeout
    }

    /// Create a new empty page in this session. Returns the page ID.
    pub fn create_page(&mut self) -> Result<String, BrowserError> {
        self.touch();
        self.page_pool.create_page()
    }

    /// Load HTML into a new page in this session. Returns the page ID.
    pub fn load_page_from_html(&mut self, html: &str) -> Result<String, BrowserError> {
        self.touch();
        self.page_pool.load_page_from_html(html)
    }

    /// Add a pre-built `Page` to this session. Returns the page ID.
    pub fn add_page(&mut self, page: Page) -> Result<String, BrowserError> {
        self.touch();
        self.page_pool.add_page(page)
    }

    /// Close a page and free its tracked resources.
    pub fn close_page(&mut self, page_id: &str) -> bool {
        self.touch();
        self.page_pool.close_page(page_id)
    }

    pub fn active_page_count(&self) -> usize {
        self.page_pool.active_page_count()
    }

    pub fn page_ids(&self) -> Vec<String> {
        self.page_pool.page_ids()
    }

    pub fn get_page(&self, page_id: &str) -> Option<&Page> {
        self.page_pool.get_page(page_id)
    }

    pub fn get_page_mut(&mut self, page_id: &str) -> Option<&mut Page> {
        self.touch();
        self.page_pool.get_page_mut(page_id)
    }

    /// Convenience: set a named cookie for a domain in this session.
    pub fn set_cookie(&mut self, name: &str, value: &str, domain: &str) {
        use crate::net::cookies::{Cookie, SameSite};
        self.cookies.store(Cookie {
            name: name.to_string(),
            value: value.to_string(),
            domain: Some(domain.to_string()),
            path: Some("/".to_string()),
            expires: None,
            secure: false,
            http_only: false,
            same_site: SameSite::Lax,
            host_only: true,
        });
    }

    /// Lookup a cookie value by name and domain.
    pub fn get_cookie(&self, name: &str, domain: &str) -> Option<String> {
        self.cookies
            .cookies_for(domain, "/", false)
            .iter()
            .find(|c| c.name == name)
            .map(|c| c.value.clone())
    }

    /// Snapshot for serialization (cookies excluded for security).
    pub fn to_data(&self) -> SessionData {
        let created_unix = self.created_at
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();
        SessionData {
            id: self.id.clone(),
            history: self.history.clone(),
            created_at_unix: created_unix,
            page_ids: self.page_pool.page_ids(),
        }
    }
}

/// Serializable snapshot of a session (no cookies, no DOM state).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SessionData {
    pub id: String,
    pub history: Vec<String>,
    pub created_at_unix: u64,
    pub page_ids: Vec<String>,
}

/// Manages all browser sessions.
pub struct SessionManager {
    sessions: HashMap<String, Session>,
    pub session_timeout: Duration,
    pub per_session_limits: ResourceLimits,
}

impl Default for SessionManager {
    fn default() -> Self {
        Self::new()
    }
}

impl SessionManager {
    pub fn new() -> Self {
        SessionManager {
            sessions: HashMap::new(),
            session_timeout: Duration::from_secs(30 * 60),
            per_session_limits: ResourceLimits::default(),
        }
    }

    pub fn with_timeout(mut self, timeout: Duration) -> Self {
        self.session_timeout = timeout;
        self
    }

    pub fn with_per_session_limits(mut self, limits: ResourceLimits) -> Self {
        self.per_session_limits = limits;
        self
    }

    /// Create a new session and return its ID.
    pub fn create_session(&mut self) -> String {
        let session = Session::new(self.per_session_limits.clone());
        let id = session.id.clone();
        self.sessions.insert(id.clone(), session);
        id
    }

    pub fn get_session(&self, id: &str) -> Option<&Session> {
        self.sessions.get(id)
    }

    pub fn get_session_mut(&mut self, id: &str) -> Option<&mut Session> {
        if let Some(s) = self.sessions.get_mut(id) {
            s.touch();
            Some(s)
        } else {
            None
        }
    }

    pub fn delete_session(&mut self, id: &str) -> bool {
        self.sessions.remove(id).is_some()
    }

    pub fn session_ids(&self) -> Vec<String> {
        self.sessions.keys().cloned().collect()
    }

    pub fn session_count(&self) -> usize {
        self.sessions.len()
    }

    /// Expire idle sessions. Returns how many were removed.
    pub fn expire_sessions(&mut self) -> usize {
        let timeout = self.session_timeout;
        let to_remove: Vec<String> = self
            .sessions
            .iter()
            .filter(|(_, s)| s.is_expired(timeout))
            .map(|(id, _)| id.clone())
            .collect();
        let count = to_remove.len();
        for id in to_remove {
            self.sessions.remove(&id);
        }
        count
    }

    pub fn total_active_pages(&self) -> usize {
        self.sessions.values().map(|s| s.active_page_count()).sum()
    }

    /// Persist all session metadata to a JSON file.
    pub fn save_to_disk(&self, path: &Path) -> Result<(), String> {
        let data: Vec<SessionData> = self.sessions.values().map(|s| s.to_data()).collect();
        let json = serde_json::to_string_pretty(&data)
            .map_err(|e| format!("serialize error: {e}"))?;
        std::fs::write(path, json).map_err(|e| format!("write error: {e}"))?;
        Ok(())
    }

    /// Restore sessions from a previously saved JSON file.
    /// Returns the number of sessions loaded.
    pub fn load_from_disk(&mut self, path: &Path) -> Result<usize, String> {
        let json = std::fs::read_to_string(path).map_err(|e| format!("read error: {e}"))?;
        let data: Vec<SessionData> =
            serde_json::from_str(&json).map_err(|e| format!("deserialize error: {e}"))?;
        let count = data.len();
        for sd in data {
            let mut session = Session::with_id(sd.id.clone(), self.per_session_limits.clone());
            session.history = sd.history;
            self.sessions.insert(sd.id, session);
        }
        Ok(count)
    }
}
