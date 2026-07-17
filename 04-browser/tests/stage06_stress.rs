//! Stage 06 — Production readiness stress tests.
//!
//! All tests are offline (no network). Concurrent scenarios use tokio tasks.

use std::path::Path;
use std::sync::Arc;
use std::time::Duration;

use agent_browser::agent::{
    error::{BrowserError, CrashRecovery, ErrorChain, RetryPolicy},
    page_pool::{PagePool, PageState},
    resource::{ResourceLimits, ResourceMonitor},
    session::{Session, SessionData, SessionManager},
    Page,
};

// ─── Helpers ──────────────────────────────────────────────────────────────────

fn html_page(title: &str) -> String {
    format!(
        r#"<!DOCTYPE html><html><head><title>{title}</title></head>
        <body><h1>{title}</h1><p>Content for {title}.</p></body></html>"#
    )
}

fn small_limits(max_pages: usize) -> ResourceLimits {
    ResourceLimits::new()
        .with_max_pages(max_pages)
        .with_max_memory_mb(256)
        .with_max_concurrent_requests(10)
}

// ─── 1. Multi-session creation ────────────────────────────────────────────────

#[test]
fn create_ten_sessions() {
    let mut manager = SessionManager::new();
    let ids: Vec<String> = (0..10).map(|_| manager.create_session()).collect();
    assert_eq!(manager.session_count(), 10);
    for id in &ids {
        assert!(
            manager.get_session(id).is_some(),
            "session {id} should exist"
        );
    }
}

#[test]
fn sessions_have_unique_ids() {
    let mut manager = SessionManager::new();
    let ids: Vec<String> = (0..20).map(|_| manager.create_session()).collect();
    let unique: std::collections::HashSet<&String> = ids.iter().collect();
    assert_eq!(unique.len(), 20, "all session IDs must be unique");
}

#[test]
fn delete_session() {
    let mut manager = SessionManager::new();
    let id = manager.create_session();
    assert!(manager.delete_session(&id));
    assert!(manager.get_session(&id).is_none());
    assert_eq!(manager.session_count(), 0);
}

// ─── 2. Multi-page support per session ───────────────────────────────────────

#[test]
fn session_creates_multiple_pages() {
    let mut manager = SessionManager::new().with_per_session_limits(small_limits(5));
    let sid = manager.create_session();
    let session = manager.get_session_mut(&sid).unwrap();

    let mut page_ids = Vec::new();
    for i in 0..5 {
        let html = html_page(&format!("Page {i}"));
        let pid = session.load_page_from_html(&html).unwrap();
        page_ids.push(pid);
    }

    assert_eq!(session.active_page_count(), 5);
    for pid in &page_ids {
        let page = session.get_page(pid).unwrap();
        assert!(!page.title().is_empty());
    }
}

#[test]
fn ten_sessions_five_pages_each() {
    let mut manager = SessionManager::new().with_per_session_limits(small_limits(10));

    let session_ids: Vec<String> = (0..10).map(|_| manager.create_session()).collect();

    for (i, sid) in session_ids.iter().enumerate() {
        let session = manager.get_session_mut(sid).unwrap();
        for j in 0..5 {
            let html = html_page(&format!("Session{i}-Page{j}"));
            session.load_page_from_html(&html).unwrap();
        }
        assert_eq!(session.active_page_count(), 5);
    }

    let total = manager.total_active_pages();
    assert_eq!(total, 50);
}

#[test]
fn close_page_reduces_count() {
    let limits = small_limits(5);
    let mut session = Session::new(limits);
    let p1 = session.load_page_from_html(&html_page("A")).unwrap();
    let p2 = session.load_page_from_html(&html_page("B")).unwrap();
    assert_eq!(session.active_page_count(), 2);
    assert!(session.close_page(&p1));
    assert_eq!(session.active_page_count(), 1);
    assert!(session.get_page(&p1).is_none());
    assert!(session.get_page(&p2).is_some());
}

// ─── 3. Session isolation ─────────────────────────────────────────────────────

#[test]
fn cookies_are_isolated_between_sessions() {
    let mut manager = SessionManager::new();
    let sid_a = manager.create_session();
    let sid_b = manager.create_session();

    // Set a cookie in session A
    {
        let session_a = manager.get_session_mut(&sid_a).unwrap();
        session_a.set_cookie("auth_token", "secret-abc", "example.com");
    }

    // Session B must not see session A's cookie
    {
        let session_b = manager.get_session_mut(&sid_b).unwrap();
        let val = session_b.get_cookie("auth_token", "example.com");
        assert!(val.is_none(), "session B must not see session A's cookie");
    }

    // Session A's cookie is still there
    {
        let session_a = manager.get_session_mut(&sid_a).unwrap();
        let val = session_a.get_cookie("auth_token", "example.com");
        assert_eq!(val.as_deref(), Some("secret-abc"));
    }
}

#[test]
fn different_cookies_per_session() {
    let mut manager = SessionManager::new();
    let sid_a = manager.create_session();
    let sid_b = manager.create_session();

    manager
        .get_session_mut(&sid_a)
        .unwrap()
        .set_cookie("user", "alice", "site.com");
    manager
        .get_session_mut(&sid_b)
        .unwrap()
        .set_cookie("user", "bob", "site.com");

    let val_a = manager
        .get_session_mut(&sid_a)
        .unwrap()
        .get_cookie("user", "site.com");
    let val_b = manager
        .get_session_mut(&sid_b)
        .unwrap()
        .get_cookie("user", "site.com");

    assert_eq!(val_a.as_deref(), Some("alice"));
    assert_eq!(val_b.as_deref(), Some("bob"));
}

#[test]
fn page_dom_isolated_between_sessions() {
    let mut manager = SessionManager::new();
    let sid_a = manager.create_session();
    let sid_b = manager.create_session();

    let pid_a = manager
        .get_session_mut(&sid_a)
        .unwrap()
        .load_page_from_html(&html_page("Session A"))
        .unwrap();
    let pid_b = manager
        .get_session_mut(&sid_b)
        .unwrap()
        .load_page_from_html(&html_page("Session B"))
        .unwrap();

    let title_a = manager
        .get_session(&sid_a)
        .unwrap()
        .get_page(&pid_a)
        .unwrap()
        .title();
    let title_b = manager
        .get_session(&sid_b)
        .unwrap()
        .get_page(&pid_b)
        .unwrap()
        .title();

    assert_eq!(title_a, "Session A");
    assert_eq!(title_b, "Session B");
    assert_ne!(title_a, title_b);
}

// ─── 4. Resource limits ───────────────────────────────────────────────────────

#[test]
fn max_pages_limit_enforced() {
    let limits = small_limits(3);
    let mut session = Session::new(limits);

    let p1 = session.create_page().unwrap();
    let p2 = session.create_page().unwrap();
    let p3 = session.create_page().unwrap();
    assert_eq!(session.active_page_count(), 3);

    // 4th page must fail
    let result = session.create_page();
    assert!(result.is_err(), "should reject page beyond limit");
    match result.unwrap_err() {
        BrowserError::ResourceLimitError { limit_type, .. } => {
            assert_eq!(limit_type, "max_pages");
        }
        other => panic!("expected ResourceLimitError, got {other:?}"),
    }

    // After closing one, a new page is allowed again
    session.close_page(&p1);
    let p4 = session.create_page();
    assert!(p4.is_ok(), "should allow page after closing one");
    let _ = (p2, p3);
}

#[test]
fn resource_monitor_tracks_usage() {
    let limits = small_limits(5);
    let mut monitor = ResourceMonitor::new(limits);

    assert_eq!(monitor.current_pages, 0);
    monitor.on_page_opened(4);
    monitor.on_page_opened(4);
    assert_eq!(monitor.current_pages, 2);
    assert_eq!(monitor.current_memory_mb, 8);

    monitor.on_page_closed(4);
    assert_eq!(monitor.current_pages, 1);
    assert_eq!(monitor.current_memory_mb, 4);
}

#[test]
fn resource_monitor_page_limit_check() {
    let limits = ResourceLimits::new()
        .with_max_pages(2)
        .with_max_memory_mb(100);
    let mut monitor = ResourceMonitor::new(limits);
    monitor.on_page_opened(1);
    monitor.on_page_opened(1);
    assert!(monitor.check_page_limit().is_err());
}

#[test]
fn page_pool_enforces_limit() {
    let limits = small_limits(2);
    let mut pool = PagePool::new(limits);
    pool.load_page_from_html("<html><body>A</body></html>")
        .unwrap();
    pool.load_page_from_html("<html><body>B</body></html>")
        .unwrap();
    let err = pool.load_page_from_html("<html><body>C</body></html>");
    assert!(err.is_err());
}

// ─── 5. Session timeout / expiry ─────────────────────────────────────────────

#[test]
fn session_expires_after_timeout() {
    let limits = small_limits(5);
    let mut session = Session::new(limits);

    // With 0-duration timeout, session is immediately expired
    assert!(session.is_expired(Duration::ZERO));

    // With a long timeout, it is not expired
    assert!(!session.is_expired(Duration::from_secs(3600)));
}

#[test]
fn session_manager_expires_idle_sessions() {
    let mut manager = SessionManager::new().with_timeout(Duration::ZERO);
    manager.create_session();
    manager.create_session();
    assert_eq!(manager.session_count(), 2);

    let removed = manager.expire_sessions();
    assert_eq!(removed, 2);
    assert_eq!(manager.session_count(), 0);
}

#[test]
fn session_timeout_only_removes_idle() {
    let mut manager = SessionManager::new().with_timeout(Duration::from_secs(3600));
    let sid = manager.create_session();
    // Touch keeps it alive
    manager.get_session_mut(&sid).unwrap().touch();
    assert_eq!(manager.expire_sessions(), 0);
    assert_eq!(manager.session_count(), 1);
}

// ─── 6. Crash recovery ───────────────────────────────────────────────────────

#[test]
fn crashed_page_is_tracked() {
    let mut recovery = CrashRecovery::new();
    assert_eq!(recovery.crashed_count(), 0);

    recovery.mark_crashed("page-1");
    assert!(recovery.is_crashed("page-1"));
    assert!(!recovery.is_crashed("page-2"));
    assert_eq!(recovery.crashed_count(), 1);
}

#[test]
fn page_recovery_clears_crash_flag() {
    let mut recovery = CrashRecovery::new();
    recovery.mark_crashed("page-1");
    recovery.recover_page("page-1");
    assert!(!recovery.is_crashed("page-1"));
    assert_eq!(recovery.crashed_count(), 0);
}

#[test]
fn session_crash_does_not_affect_other_pages() {
    let limits = small_limits(5);
    let mut session = Session::new(limits);
    let p1 = session.load_page_from_html(&html_page("Page 1")).unwrap();
    let p2 = session.load_page_from_html(&html_page("Page 2")).unwrap();
    let p3 = session.load_page_from_html(&html_page("Page 3")).unwrap();

    // Simulate page-2 JS context crash
    session.crash_recovery.mark_crashed(&p2);

    // Pages 1 and 3 are unaffected
    assert!(!session.crash_recovery.is_crashed(&p1));
    assert!(!session.crash_recovery.is_crashed(&p3));

    // Session is still active with 3 pages
    assert_eq!(session.active_page_count(), 3);

    // DOM content of non-crashed pages is still accessible
    assert_eq!(session.get_page(&p1).unwrap().title(), "Page 1");
    assert_eq!(session.get_page(&p3).unwrap().title(), "Page 3");

    // Crashed page count
    assert_eq!(session.crash_recovery.crashed_count(), 1);
}

#[test]
fn multiple_crashed_pages_tracked() {
    let mut recovery = CrashRecovery::new();
    recovery.mark_crashed("p1");
    recovery.mark_crashed("p2");
    recovery.mark_crashed("p3");
    assert_eq!(recovery.crashed_count(), 3);

    recovery.recover_page("p2");
    assert_eq!(recovery.crashed_count(), 2);
    assert!(!recovery.is_crashed("p2"));
    assert!(recovery.is_crashed("p1"));
    assert!(recovery.is_crashed("p3"));
}

// ─── 7. Error types ───────────────────────────────────────────────────────────

#[test]
fn browser_error_display() {
    let e = BrowserError::NetworkError {
        message: "connection refused".to_string(),
        url: Some("https://example.com".to_string()),
    };
    let s = format!("{e}");
    assert!(s.contains("NetworkError"));
    assert!(s.contains("connection refused"));
    assert!(s.contains("example.com"));
}

#[test]
fn error_chain_with_context() {
    let e = BrowserError::JsError {
        message: "ReferenceError: x is not defined".to_string(),
        stack: Some("at eval:1:1".to_string()),
    };
    let chain = ErrorChain::new(e)
        .with_page("page-42")
        .with_session("session-99")
        .with_operation("eval");
    let s = format!("{chain}");
    assert!(s.contains("JsError"));
    assert!(s.contains("page-42"));
    assert!(s.contains("session-99"));
    assert!(s.contains("eval"));
}

#[test]
fn timeout_error_variant() {
    let e = BrowserError::TimeoutError {
        message: "navigation timed out".to_string(),
        elapsed: Some(Duration::from_secs(30)),
    };
    let s = format!("{e}");
    assert!(s.contains("TimeoutError"));
    assert!(s.contains("30s"));
}

#[test]
fn resource_limit_error_variant() {
    let e = BrowserError::ResourceLimitError {
        message: "max pages exceeded".to_string(),
        limit_type: "max_pages".to_string(),
    };
    let s = format!("{e}");
    assert!(s.contains("max_pages"));
}

// ─── 8. Retry policy ─────────────────────────────────────────────────────────

#[test]
fn retry_policy_default() {
    let policy = RetryPolicy::default();
    assert_eq!(policy.max_attempts, 3);
    assert!(policy.should_retry(0));
    assert!(policy.should_retry(2));
    assert!(!policy.should_retry(3));
}

#[test]
fn retry_policy_no_retry() {
    let policy = RetryPolicy::no_retry();
    assert!(!policy.should_retry(0));
}

#[test]
fn retry_policy_exponential_backoff() {
    let policy = RetryPolicy::default();
    let d0 = policy.delay_for_attempt(0);
    let d1 = policy.delay_for_attempt(1);
    let d2 = policy.delay_for_attempt(2);
    // Each step should be at least as long as the previous
    assert!(d1 >= d0);
    assert!(d2 >= d1);
    // Never exceeds max_delay
    assert!(d2 <= policy.max_delay);
}

// ─── 9. Session persistence (save + restore) ─────────────────────────────────

#[test]
fn save_and_restore_sessions() {
    let tmp = std::env::temp_dir().join("agent_browser_test_sessions.json");

    // Create and populate sessions
    {
        let mut manager = SessionManager::new();
        let sid_a = manager.create_session();
        let sid_b = manager.create_session();

        {
            let sa = manager.get_session_mut(&sid_a).unwrap();
            sa.history.push("https://example.com/a".to_string());
            sa.set_cookie("auth", "token-a", "example.com");
            sa.load_page_from_html("<html><body>A</body></html>").unwrap();
        }

        {
            let sb = manager.get_session_mut(&sid_b).unwrap();
            sb.history.push("https://example.com/b".to_string());
            sb.set_cookie("auth", "token-b", "example.com");
        }

        manager.save_to_disk(&tmp).expect("save failed");
    }

    // Load sessions in a fresh manager
    {
        let mut manager = SessionManager::new();
        let loaded = manager.load_from_disk(&tmp).expect("load failed");
        assert_eq!(loaded, 2);
        assert_eq!(manager.session_count(), 2);

        // History is preserved
        let sessions: Vec<_> = manager.session_ids();
        let expected_urls: std::collections::HashSet<String> = sessions
            .iter()
            .flat_map(|id| manager.get_session(id).unwrap().history.iter().cloned())
            .collect();
        assert!(expected_urls.contains("https://example.com/a"));
        assert!(expected_urls.contains("https://example.com/b"));

        // Cookies are preserved
        let mut auth_values: Vec<String> = sessions
            .iter()
            .filter_map(|id| manager.get_session(id).unwrap().get_cookie("auth", "example.com"))
            .collect();
        auth_values.sort();
        assert_eq!(auth_values, vec!["token-a".to_string(), "token-b".to_string()]);

        // Pages are NOT restored — pool is empty (honesty contract)
        for id in &sessions {
            assert_eq!(
                manager.get_session(id).unwrap().active_page_count(),
                0,
                "restored session must not rehydrate dead page IDs"
            );
        }
    }

    // Clean up
    let _ = std::fs::remove_file(&tmp);
}

#[test]
fn session_data_serialization() {
    let data = SessionData {
        id: "test-session-id".to_string(),
        history: vec!["https://example.com".to_string()],
        created_at_unix: 1_700_000_000,
        cookies: vec![],
        last_page_ids: vec!["page-1".to_string(), "page-2".to_string()],
        pages_restored: false,
    };
    let json = serde_json::to_string(&data).unwrap();
    let restored: SessionData = serde_json::from_str(&json).unwrap();
    assert_eq!(restored.id, "test-session-id");
    assert_eq!(restored.history.len(), 1);
    assert_eq!(restored.last_page_ids.len(), 2);
    assert_eq!(restored.created_at_unix, 1_700_000_000);
    assert!(!restored.pages_restored);
}

#[test]
fn session_data_accepts_legacy_page_ids_field() {
    // Old snapshots used `page_ids`; new code reads it as last_page_ids.
    let json = r#"{
        "id": "legacy",
        "history": [],
        "created_at_unix": 1,
        "page_ids": ["p1"]
    }"#;
    let restored: SessionData = serde_json::from_str(json).unwrap();
    assert_eq!(restored.last_page_ids, vec!["p1".to_string()]);
    assert!(restored.cookies.is_empty());
}

// ─── 10. Graceful-shutdown simulation ────────────────────────────────────────

#[test]
fn graceful_shutdown_saves_sessions() {
    let tmp = std::env::temp_dir().join("agent_browser_shutdown_test.json");

    let mut manager = SessionManager::new().with_per_session_limits(small_limits(5));

    // Simulate active sessions
    for i in 0..5 {
        let sid = manager.create_session();
        let session = manager.get_session_mut(&sid).unwrap();
        session.history.push(format!("https://site{i}.example.com"));
        session
            .load_page_from_html(&html_page(&format!("Site {i}")))
            .unwrap();
    }

    assert_eq!(manager.session_count(), 5);
    assert_eq!(manager.total_active_pages(), 5);

    // Simulate SIGTERM: save sessions to disk
    manager.save_to_disk(&tmp).expect("shutdown save failed");

    // Simulate process restart: load sessions back
    let mut restored_manager = SessionManager::new();
    let count = restored_manager
        .load_from_disk(&tmp)
        .expect("reload failed");
    assert_eq!(count, 5);
    assert_eq!(restored_manager.session_count(), 5);

    let _ = std::fs::remove_file(&tmp);
}

// ─── 11. Concurrent access (tokio multi-task) ────────────────────────────────

#[tokio::test]
async fn concurrent_session_creation() {
    let manager = Arc::new(tokio::sync::Mutex::new(
        SessionManager::new().with_per_session_limits(small_limits(10)),
    ));

    let mut handles = Vec::new();
    for i in 0..10 {
        let mgr = Arc::clone(&manager);
        handles.push(tokio::spawn(async move {
            let mut m = mgr.lock().await;
            let sid = m.create_session();
            let session = m.get_session_mut(&sid).unwrap();
            let html = format!("<html><head><title>Task {i}</title></head><body></body></html>");
            session.load_page_from_html(&html).unwrap()
        }));
    }

    let page_ids: Vec<String> = futures_util::future::join_all(handles)
        .await
        .into_iter()
        .map(|r| r.expect("task panicked"))
        .collect();

    let m = manager.lock().await;
    assert_eq!(m.session_count(), 10);
    assert_eq!(m.total_active_pages(), 10);
    assert_eq!(page_ids.len(), 10);
}

#[tokio::test]
async fn concurrent_page_reads_same_session() {
    // Demonstrate that page content from different sessions can be read concurrently.
    let mut manager = SessionManager::new().with_per_session_limits(small_limits(10));

    let mut page_data: Vec<(String, String)> = Vec::new(); // (session_id, page_id)
    for i in 0..5 {
        let sid = manager.create_session();
        let session = manager.get_session_mut(&sid).unwrap();
        let html = html_page(&format!("Concurrent {i}"));
        let pid = session.load_page_from_html(&html).unwrap();
        page_data.push((sid, pid));
    }

    // Read all pages concurrently via spawn_blocking (DOM access is sync)
    let manager = Arc::new(std::sync::Mutex::new(manager));
    let mut handles = Vec::new();
    for (sid, pid) in page_data.clone() {
        let mgr = Arc::clone(&manager);
        handles.push(tokio::task::spawn_blocking(move || {
            let m = mgr.lock().unwrap();
            m.get_session(&sid)
                .and_then(|s| s.get_page(&pid))
                .map(|p| p.title())
                .unwrap_or_default()
        }));
    }

    let titles: Vec<String> = futures_util::future::join_all(handles)
        .await
        .into_iter()
        .map(|r| r.expect("task panicked"))
        .collect();

    assert_eq!(titles.len(), 5);
    for (i, title) in titles.iter().enumerate() {
        assert_eq!(
            title,
            &format!("Concurrent {i}"),
            "title mismatch at index {i}"
        );
    }
}

// ─── 12. ResourceLimits configuration ────────────────────────────────────────

#[test]
fn resource_limits_builder() {
    let limits = ResourceLimits::new()
        .with_max_pages(20)
        .with_max_memory_mb(1024)
        .with_max_concurrent_requests(100)
        .with_request_timeout(Duration::from_secs(60));

    assert_eq!(limits.max_pages, 20);
    assert_eq!(limits.max_memory_mb, 1024);
    assert_eq!(limits.max_concurrent_requests, 100);
    assert_eq!(limits.request_timeout, Duration::from_secs(60));
}

#[test]
fn resource_limits_defaults() {
    let limits = ResourceLimits::default();
    assert_eq!(limits.max_pages, 10);
    assert_eq!(limits.max_memory_mb, 512);
    assert_eq!(limits.request_timeout, Duration::from_secs(30));
}

#[test]
fn resource_monitor_approaching_limits() {
    let limits = ResourceLimits::new()
        .with_max_pages(10)
        .with_max_memory_mb(100);
    let mut monitor = ResourceMonitor::new(limits);

    // Not near limits yet
    assert!(!monitor.is_approaching_page_limit());
    assert!(!monitor.is_approaching_memory_limit());

    // Add pages until near limit
    for _ in 0..8 {
        monitor.on_page_opened(1);
    }
    assert!(monitor.is_approaching_page_limit()); // 8+2 >= 10
    assert!(!monitor.is_approaching_memory_limit()); // 8% used

    // Add memory until near limit
    monitor.on_page_opened(75); // now at 83MB of 100MB = 83%
    assert!(monitor.is_approaching_memory_limit());
}

// ─── 13. Page pool lifecycle ─────────────────────────────────────────────────

#[test]
fn page_pool_state_transitions() {
    let limits = small_limits(5);
    let mut pool = PagePool::new(limits);

    let pid = pool.create_page().unwrap();
    assert_eq!(pool.get_page_state(&pid), Some(&PageState::Loaded));

    pool.set_page_state(&pid, PageState::Loading);
    assert_eq!(pool.get_page_state(&pid), Some(&PageState::Loading));

    pool.set_page_state(&pid, PageState::Error("timeout".to_string()));
    assert_eq!(
        pool.get_page_state(&pid),
        Some(&PageState::Error("timeout".to_string()))
    );
}

#[test]
fn page_pool_closed_page_invisible() {
    let limits = small_limits(5);
    let mut pool = PagePool::new(limits);
    let pid = pool.create_page().unwrap();

    assert!(pool.get_page(&pid).is_some());
    pool.close_page(&pid);
    assert!(pool.get_page(&pid).is_none());
    assert_eq!(pool.active_page_count(), 0);
    // Closed pages are not in page_ids()
    assert!(!pool.page_ids().contains(&pid));
}

#[test]
fn page_pool_memory_tracking() {
    let limits = small_limits(5);
    let mut pool = PagePool::new(limits);
    assert_eq!(pool.monitor().current_pages, 0);

    let p1 = pool.create_page().unwrap();
    assert_eq!(pool.monitor().current_pages, 1);

    pool.close_page(&p1);
    assert_eq!(pool.monitor().current_pages, 0);
    assert_eq!(pool.monitor().current_memory_mb, 0);
}
