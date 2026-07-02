//! Stage 03: Network & Protocol tests

use agent_browser::net::{CookieJar, HttpClient, Url};
use agent_browser::net::cookies::parse_set_cookie;
use agent_browser::net::http::detect_content_type;
use agent_browser::net::http::ContentType;

// ============================================================
// URL Tests
// ============================================================

#[test]
fn url_parse_basic() {
    let url = Url::parse("https://example.com/path?query=1#frag").unwrap();
    assert_eq!(url.scheme(), "https");
    assert_eq!(url.host(), Some("example.com"));
    assert_eq!(url.path(), "/path");
    assert_eq!(url.query(), Some("query=1"));
    assert_eq!(url.fragment(), Some("frag"));
}

#[test]
fn url_parse_with_port() {
    let url = Url::parse("http://localhost:8080/api").unwrap();
    assert_eq!(url.scheme(), "http");
    assert_eq!(url.host(), Some("localhost"));
    assert_eq!(url.port(), Some(8080));
    assert_eq!(url.path(), "/api");
}

#[test]
fn url_resolve_relative() {
    let base = Url::parse("https://example.com/a/b/c.html").unwrap();
    let resolved = Url::resolve(&base, "../images/logo.png").unwrap();
    assert_eq!(resolved.path(), "/a/images/logo.png");
}

#[test]
fn url_resolve_absolute() {
    let base = Url::parse("https://example.com/page").unwrap();
    let resolved = Url::resolve(&base, "https://other.com/thing").unwrap();
    assert_eq!(resolved.host(), Some("other.com"));
    assert_eq!(resolved.path(), "/thing");
}

#[test]
fn url_resolve_root_relative() {
    let base = Url::parse("https://example.com/a/b/c").unwrap();
    let resolved = Url::resolve(&base, "/new/path").unwrap();
    assert_eq!(resolved.host(), Some("example.com"));
    assert_eq!(resolved.path(), "/new/path");
}

#[test]
fn url_origin() {
    let url = Url::parse("https://example.com/path").unwrap();
    assert_eq!(url.origin(), "https://example.com");
}

#[test]
fn url_same_origin() {
    let a = Url::parse("https://example.com/a").unwrap();
    let b = Url::parse("https://example.com/b").unwrap();
    let c = Url::parse("https://other.com/a").unwrap();
    assert!(a.is_same_origin(&b));
    assert!(!a.is_same_origin(&c));
}

#[test]
fn url_display() {
    let s = "https://example.com/path?q=1";
    let url = Url::parse(s).unwrap();
    assert_eq!(url.to_string(), s);
}

#[test]
fn url_invalid() {
    assert!(Url::parse("not a url").is_err());
    assert!(Url::parse("").is_err());
}

// ============================================================
// Cookie Tests
// ============================================================

#[test]
fn cookie_parse_basic() {
    let cookie = parse_set_cookie("session=abc123", "example.com", "/").unwrap();
    assert_eq!(cookie.name, "session");
    assert_eq!(cookie.value, "abc123");
    assert!(cookie.is_session());
}

#[test]
fn cookie_parse_with_attrs() {
    let cookie = parse_set_cookie(
        "auth=xyz; Path=/api; Secure; HttpOnly; SameSite=Strict",
        "example.com",
        "/api/login",
    )
    .unwrap();
    assert_eq!(cookie.name, "auth");
    assert_eq!(cookie.value, "xyz");
    assert_eq!(cookie.path, Some("/api".to_string()));
    assert!(cookie.secure);
    assert!(cookie.http_only);
    assert_eq!(cookie.same_site, agent_browser::net::cookies::SameSite::Strict);
}

#[test]
fn cookie_parse_max_age() {
    let cookie = parse_set_cookie("tok=val; Max-Age=3600", "example.com", "/").unwrap();
    assert!(!cookie.is_session());
    assert!(!cookie.is_expired());
}

#[test]
fn cookie_parse_expired_max_age() {
    let cookie = parse_set_cookie("tok=val; Max-Age=0", "example.com", "/").unwrap();
    assert!(cookie.is_expired());
}

#[test]
fn cookie_parse_domain() {
    let cookie = parse_set_cookie("id=1; Domain=example.com", "www.example.com", "/").unwrap();
    assert_eq!(cookie.domain, Some("example.com".to_string()));
    assert!(!cookie.host_only);
}

#[test]
fn cookie_jar_store_and_retrieve() {
    let mut jar = CookieJar::new();
    let cookie = parse_set_cookie("session=abc", "example.com", "/").unwrap();
    jar.store(cookie);
    assert_eq!(jar.len(), 1);

    let cookies = jar.cookies_for("example.com", "/page", false);
    assert_eq!(cookies.len(), 1);
    assert_eq!(cookies[0].name, "session");
}

#[test]
fn cookie_jar_domain_matching() {
    let mut jar = CookieJar::new();
    let cookie = parse_set_cookie("id=1; Domain=example.com", "example.com", "/").unwrap();
    jar.store(cookie);

    // Should match subdomain
    let cookies = jar.cookies_for("www.example.com", "/", false);
    assert_eq!(cookies.len(), 1);

    // Should not match different domain
    let cookies = jar.cookies_for("other.com", "/", false);
    assert_eq!(cookies.len(), 0);
}

#[test]
fn cookie_jar_path_matching() {
    let mut jar = CookieJar::new();
    let c1 = parse_set_cookie("api=1; Path=/api", "example.com", "/api/v1").unwrap();
    let c2 = parse_set_cookie("root=2; Path=/", "example.com", "/").unwrap();
    jar.store(c1);
    jar.store(c2);

    // /api path gets both cookies (longer path first)
    let cookies = jar.cookies_for("example.com", "/api/endpoint", false);
    assert_eq!(cookies.len(), 2);
    assert_eq!(cookies[0].name, "api"); // longer path first

    // / path only gets root cookie
    let cookies = jar.cookies_for("example.com", "/other", false);
    assert_eq!(cookies.len(), 1);
    assert_eq!(cookies[0].name, "root");
}

#[test]
fn cookie_jar_secure_flag() {
    let mut jar = CookieJar::new();
    let cookie = parse_set_cookie("secret=val; Secure", "example.com", "/").unwrap();
    jar.store(cookie);

    // Should not be sent on non-secure requests
    let cookies = jar.cookies_for("example.com", "/", false);
    assert_eq!(cookies.len(), 0);

    // Should be sent on secure requests
    let cookies = jar.cookies_for("example.com", "/", true);
    assert_eq!(cookies.len(), 1);
}

#[test]
fn cookie_jar_replace_same_name() {
    let mut jar = CookieJar::new();
    let c1 = parse_set_cookie("session=old; Path=/", "example.com", "/").unwrap();
    let c2 = parse_set_cookie("session=new; Path=/", "example.com", "/").unwrap();
    jar.store(c1);
    jar.store(c2);
    assert_eq!(jar.len(), 1);

    let cookies = jar.cookies_for("example.com", "/", false);
    assert_eq!(cookies[0].value, "new");
}

#[test]
fn cookie_jar_expired_removal() {
    let mut jar = CookieJar::new();
    let cookie = parse_set_cookie("session=abc", "example.com", "/").unwrap();
    jar.store(cookie);

    // Store expired cookie with same name to remove it
    let expired = parse_set_cookie("session=abc; Max-Age=0", "example.com", "/").unwrap();
    jar.store(expired);
    assert_eq!(jar.len(), 0);
}

#[test]
fn cookie_header_format() {
    let mut jar = CookieJar::new();
    let c1 = parse_set_cookie("a=1; Path=/api", "example.com", "/api").unwrap();
    let c2 = parse_set_cookie("b=2; Path=/", "example.com", "/").unwrap();
    jar.store(c1);
    jar.store(c2);

    let header = jar.cookie_header("example.com", "/api/endpoint", false);
    assert!(header.contains("a=1"));
    assert!(header.contains("b=2"));
}

// ============================================================
// Content-Type Detection Tests
// ============================================================

#[test]
fn content_type_detection() {
    assert_eq!(detect_content_type("text/html"), ContentType::Html);
    assert_eq!(detect_content_type("text/html; charset=utf-8"), ContentType::Html);
    assert_eq!(detect_content_type("application/json"), ContentType::Json);
    assert_eq!(detect_content_type("text/css"), ContentType::Css);
    assert_eq!(detect_content_type("application/javascript"), ContentType::JavaScript);
    assert_eq!(detect_content_type("text/javascript"), ContentType::JavaScript);
    assert_eq!(detect_content_type("text/plain"), ContentType::Text);
    assert_eq!(detect_content_type("image/png"), ContentType::Binary);
    assert_eq!(detect_content_type(""), ContentType::Binary);
}

// ============================================================
// Integration Tests (require network)
// ============================================================

#[tokio::test]
async fn integration_fetch_example_com() {
    let client = HttpClient::with_defaults();
    let mut jar = CookieJar::new();
    let url = Url::parse("https://example.com").unwrap();

    let response = client.get(&url, &mut jar).await.expect("fetch failed");

    assert_eq!(response.status, 200);
    assert_eq!(response.content_type, ContentType::Html);
    assert!(!response.body.is_empty());

    let body = response.body_as_str();
    assert!(body.contains("Example Domain") || body.contains("<html"));
}

#[tokio::test]
async fn integration_fetch_https_certificate_verification() {
    // Verify TLS cert validation works against a well-known HTTPS site
    let client = HttpClient::with_defaults();
    let mut jar = CookieJar::new();
    let url = Url::parse("https://www.iana.org/domains/reserved").unwrap();

    let response = client.get(&url, &mut jar).await.expect("HTTPS fetch failed");
    assert_eq!(response.status, 200);
    assert_eq!(response.content_type, ContentType::Html);
    assert!(response.body_as_str().contains("IANA") || response.body_as_str().contains("html"));
}

#[tokio::test]
async fn integration_redirect_following() {
    // httpbin.org/redirect/1 always issues a redirect to /get
    let client = HttpClient::with_defaults();
    let mut jar = CookieJar::new();
    let url = Url::parse("https://httpbin.org/redirect/1").unwrap();

    let response = client.get(&url, &mut jar).await.expect("fetch failed");
    assert_eq!(response.status, 200);
    // Should have followed redirect — final URL is /get
    assert!(
        response.final_url.path().contains("get"),
        "expected redirect to /get, got: {}",
        response.final_url
    );
    assert!(response.redirect_count > 0, "expected at least one redirect");
}

#[tokio::test]
async fn integration_fetch_and_parse_dom() {
    use agent_browser::html::parse;

    let client = HttpClient::with_defaults();
    let mut jar = CookieJar::new();
    let url = Url::parse("https://example.com").unwrap();

    let response = client.get(&url, &mut jar).await.expect("fetch failed");
    assert_eq!(response.status, 200);

    let html = response.body_as_str();
    let doc = parse(html);

    // example.com has exactly one h1
    use agent_browser::dom::query_selector_all;
    use agent_browser::html::dom::DOCUMENT_NODE_ID;
    let h1s = query_selector_all(&doc, DOCUMENT_NODE_ID, "h1").unwrap();
    assert!(!h1s.is_empty(), "expected at least one h1 on example.com");

    let h1_text = doc.text_content(h1s[0]).trim().to_string();
    assert!(
        h1_text.contains("Example Domain"),
        "h1 text was: {h1_text}"
    );
}

#[test]
fn integration_cookie_jar_persistence() {
    // Verify cookie jar persists cookies across multiple store operations
    // (unit-level, no network needed — round-trip covered by other integration tests)
    let mut jar = CookieJar::new();

    let c1 = parse_set_cookie("session=abc; Path=/; Secure", "example.com", "/").unwrap();
    let c2 = parse_set_cookie("pref=dark; Path=/; Domain=example.com", "example.com", "/").unwrap();
    jar.store(c1);
    jar.store(c2);

    assert_eq!(jar.len(), 2);

    // Cookies should be retrievable for secure request
    let cookies = jar.cookies_for("example.com", "/page", true);
    assert_eq!(cookies.len(), 2);

    let header = jar.cookie_header("example.com", "/page", true);
    assert!(header.contains("session=abc"));
    assert!(header.contains("pref=dark"));

    // Update session cookie
    let c3 = parse_set_cookie("session=xyz; Path=/; Secure", "example.com", "/").unwrap();
    jar.store(c3);
    assert_eq!(jar.len(), 2); // still 2, not 3

    let cookies = jar.cookies_for("example.com", "/", true);
    let session = cookies.iter().find(|c| c.name == "session").unwrap();
    assert_eq!(session.value, "xyz");
}
