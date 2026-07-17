//! BrowserError — typed error hierarchy for Stage 06.

use std::collections::HashSet;
use std::fmt;
use std::time::Duration;

/// Structured browser error variants with context.
#[derive(Debug, Clone)]
pub enum BrowserError {
    NetworkError {
        message: String,
        url: Option<String>,
    },
    ParseError {
        message: String,
    },
    JsError {
        message: String,
        stack: Option<String>,
    },
    TimeoutError {
        message: String,
        elapsed: Option<Duration>,
    },
    ResourceLimitError {
        message: String,
        limit_type: String,
    },
    SessionError {
        message: String,
        session_id: Option<String>,
    },
}

impl fmt::Display for BrowserError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            BrowserError::NetworkError { message, url } => {
                write!(f, "NetworkError: {message}")?;
                if let Some(u) = url {
                    write!(f, " (url: {u})")?;
                }
                Ok(())
            }
            BrowserError::ParseError { message } => write!(f, "ParseError: {message}"),
            BrowserError::JsError { message, stack } => {
                write!(f, "JsError: {message}")?;
                if let Some(s) = stack {
                    write!(f, "\n{s}")?;
                }
                Ok(())
            }
            BrowserError::TimeoutError { message, elapsed } => {
                write!(f, "TimeoutError: {message}")?;
                if let Some(e) = elapsed {
                    write!(f, " (elapsed: {e:?})")?;
                }
                Ok(())
            }
            BrowserError::ResourceLimitError {
                message,
                limit_type,
            } => {
                write!(f, "ResourceLimitError[{limit_type}]: {message}")
            }
            BrowserError::SessionError {
                message,
                session_id,
            } => {
                write!(f, "SessionError: {message}")?;
                if let Some(id) = session_id {
                    write!(f, " (session: {id})")?;
                }
                Ok(())
            }
        }
    }
}

impl std::error::Error for BrowserError {}

/// Tracks which page + session an error originated from.
#[derive(Debug, Clone)]
pub struct ErrorChain {
    pub error: BrowserError,
    pub page_id: Option<String>,
    pub session_id: Option<String>,
    pub operation: Option<String>,
}

impl ErrorChain {
    pub fn new(error: BrowserError) -> Self {
        ErrorChain {
            error,
            page_id: None,
            session_id: None,
            operation: None,
        }
    }

    pub fn with_page(mut self, page_id: impl Into<String>) -> Self {
        self.page_id = Some(page_id.into());
        self
    }

    pub fn with_session(mut self, session_id: impl Into<String>) -> Self {
        self.session_id = Some(session_id.into());
        self
    }

    pub fn with_operation(mut self, op: impl Into<String>) -> Self {
        self.operation = Some(op.into());
        self
    }
}

impl fmt::Display for ErrorChain {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.error)?;
        if let Some(op) = &self.operation {
            write!(f, " [op: {op}]")?;
        }
        if let Some(sid) = &self.session_id {
            write!(f, " [session: {sid}]")?;
        }
        if let Some(pid) = &self.page_id {
            write!(f, " [page: {pid}]")?;
        }
        Ok(())
    }
}

/// Configures retry behaviour for transient failures.
#[derive(Debug, Clone)]
pub struct RetryPolicy {
    pub max_attempts: u32,
    pub base_delay: Duration,
    pub max_delay: Duration,
}

impl Default for RetryPolicy {
    fn default() -> Self {
        RetryPolicy {
            max_attempts: 3,
            base_delay: Duration::from_millis(100),
            max_delay: Duration::from_secs(5),
        }
    }
}

impl RetryPolicy {
    pub fn no_retry() -> Self {
        RetryPolicy {
            max_attempts: 0,
            base_delay: Duration::ZERO,
            max_delay: Duration::ZERO,
        }
    }

    /// Exponential backoff delay for a given attempt number (0-indexed).
    pub fn delay_for_attempt(&self, attempt: u32) -> Duration {
        let factor = 1u64 << attempt.min(10);
        let millis = self.base_delay.as_millis() as u64 * factor;
        Duration::from_millis(millis.min(self.max_delay.as_millis() as u64))
    }

    pub fn should_retry(&self, attempt: u32) -> bool {
        attempt < self.max_attempts
    }
}

/// Tracks which pages within a session have crashed JS contexts.
/// A crashed page still serves DOM reads but will not execute JS.
#[derive(Debug, Clone, Default)]
pub struct CrashRecovery {
    crashed_pages: HashSet<String>,
}

impl CrashRecovery {
    pub fn new() -> Self {
        CrashRecovery {
            crashed_pages: HashSet::new(),
        }
    }

    pub fn mark_crashed(&mut self, page_id: &str) {
        self.crashed_pages.insert(page_id.to_string());
    }

    pub fn is_crashed(&self, page_id: &str) -> bool {
        self.crashed_pages.contains(page_id)
    }

    /// Reset a page's crash state (e.g. after re-creating the JS context).
    pub fn recover_page(&mut self, page_id: &str) {
        self.crashed_pages.remove(page_id);
    }

    pub fn crashed_count(&self) -> usize {
        self.crashed_pages.len()
    }

    pub fn crashed_page_ids(&self) -> Vec<String> {
        self.crashed_pages.iter().cloned().collect()
    }
}
