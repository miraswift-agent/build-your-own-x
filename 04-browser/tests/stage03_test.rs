//! Stage 03: Network & Protocol tests

use agent_browser::net::cookies::parse_set_cookie;
use agent_browser::net::http::detect_content_type;
use agent_browser::net::http::ContentType;
use agent_browser::net::{CookieJar, HttpClient, Url};

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
    assert_eq!(
        cookie.same_site,
        agent_browser::net::cookies::SameSite::Strict
    );
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
fn cookie_parse_rejects_cross_site_domain_injection() {
    let cookie = parse_set_cookie("id=1; Domain=bank.com", "attacker.com", "/");
    assert!(cookie.is_none());
}

#[test]
fn cookie_parse_rejects_obvious_supercookies() {
    assert!(parse_set_cookie("id=1; Domain=com", "example.com", "/").is_none());
    assert!(parse_set_cookie("id=1; Domain=co.uk", "shop.co.uk", "/").is_none());
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
    assert_eq!(
        detect_content_type("text/html; charset=utf-8"),
        ContentType::Html
    );
    assert_eq!(detect_content_type("application/json"), ContentType::Json);
    assert_eq!(detect_content_type("text/css"), ContentType::Css);
    assert_eq!(
        detect_content_type("application/javascript"),
        ContentType::JavaScript
    );
    assert_eq!(
        detect_content_type("text/javascript"),
        ContentType::JavaScript
    );
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

    let body = response.body_as_str().expect("utf8 body");
    // Deliberately NO assertion about example.com's current markup. This test's
    // subject is our client (TLS, status, content-type, UTF-8 body) — not their
    // HTML. Asserting a third party's DOM is what rotted this suite; see
    // docs/browser-harden-hermetic-fixtures-closeout.md.
    assert!(!body.trim().is_empty(), "example.com returned an empty body");
}

#[tokio::test]
async fn integration_fetch_https_certificate_verification() {
    // Verify TLS cert validation works against a well-known HTTPS site
    let client = HttpClient::with_defaults();
    let mut jar = CookieJar::new();
    let url = Url::parse("https://www.iana.org/domains/reserved").unwrap();

    let response = client
        .get(&url, &mut jar)
        .await
        .expect("HTTPS fetch failed");
    assert_eq!(response.status, 200);
    assert_eq!(response.content_type, ContentType::Html);
    let body = response.body_as_str().expect("utf8 body");
    // Same rule as the example.com test: assert our behavior, not their markup.
    assert!(!body.trim().is_empty(), "iana.org returned an empty body");
}

#[tokio::test]
async fn integration_redirect_following() {
    // Local redirect server — no flaky external dependency (httpbin 503s).
    use tokio::io::{AsyncReadExt, AsyncWriteExt};
    use tokio::net::TcpListener;

    let listener = TcpListener::bind("127.0.0.1:0").await.expect("bind");
    let addr = listener.local_addr().expect("local_addr");
    let port = addr.port();

    let server = tokio::spawn(async move {
        // First connection: 302 to /final
        let (mut sock, _) = listener.accept().await.expect("accept redirect");
        let mut buf = [0u8; 1024];
        let _ = sock.read(&mut buf).await;
        let resp = format!(
            "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:{port}/final\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
        );
        sock.write_all(resp.as_bytes()).await.expect("write 302");
        drop(sock);

        // Second connection: 200 body
        let (mut sock, _) = listener.accept().await.expect("accept final");
        let mut buf = [0u8; 1024];
        let _ = sock.read(&mut buf).await;
        let body = b"redirect-ok";
        let resp = format!(
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
            body.len()
        );
        sock.write_all(resp.as_bytes()).await.expect("write 200");
        sock.write_all(body).await.expect("write body");
    });

    // Give the server a moment; client follows the redirect itself.
    let client = HttpClient::with_defaults();
    let mut jar = CookieJar::new();
    let url = Url::parse(&format!("http://127.0.0.1:{port}/start")).unwrap();

    let response = client.get(&url, &mut jar).await.expect("fetch failed");
    assert_eq!(response.status, 200);
    assert_eq!(response.final_url.path(), "/final");
    assert!(
        response.redirect_count > 0,
        "expected at least one redirect"
    );
    assert_eq!(response.body_as_str().expect("utf8 body"), "redirect-ok");

    server.await.expect("server task");
}

#[tokio::test]
async fn integration_fetch_and_parse_dom() {
    // Local server serving a snapshot of a real-world page — no third-party
    // markup dependency. This test used to fetch example.com LIVE and assert
    // it had an <h1>. On 2026-11-11 example.com dropped its <h1> (the page
    // name moved into <title>) and the test rotted. The parser was never
    // wrong. See docs/browser-harden-hermetic-fixtures-closeout.md.
    use agent_browser::html::parse;
    use tokio::io::{AsyncReadExt, AsyncWriteExt};
    use tokio::net::TcpListener;

    let html = include_str!("fixtures/14_example_com_snapshot.html");

    let listener = TcpListener::bind("127.0.0.1:0").await.expect("bind");
    let addr = listener.local_addr().expect("local_addr");
    let port = addr.port();

    let server = tokio::spawn(async move {
        let (mut sock, _) = listener.accept().await.expect("accept");
        let mut buf = [0u8; 1024];
        let _ = sock.read(&mut buf).await;
        let resp = format!(
            "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
            html.len()
        );
        sock.write_all(resp.as_bytes()).await.expect("write 200");
        sock.write_all(html.as_bytes()).await.expect("write body");
    });

    let client = HttpClient::with_defaults();
    let mut jar = CookieJar::new();
    let url = Url::parse(&format!("http://127.0.0.1:{port}/")).unwrap();

    let response = client.get(&url, &mut jar).await.expect("fetch failed");
    assert_eq!(response.status, 200);

    let body = response.body_as_str().expect("utf8 body");
    let doc = parse(body);

    use agent_browser::dom::query_selector_all;
    use agent_browser::html::dom::DOCUMENT_NODE_ID;

    // The <p> the snapshot actually has — the element example.com serves today.
    let ps = query_selector_all(&doc, DOCUMENT_NODE_ID, "p").unwrap();
    assert_eq!(ps.len(), 1, "expected exactly one <p> in the snapshot");
    assert!(
        doc.text_content(ps[0]).contains("documentation examples"),
        "p text was: {}",
        doc.text_content(ps[0])
    );

    // Child combinator still resolves on the snapshot.
    let body_ps = query_selector_all(&doc, DOCUMENT_NODE_ID, "body > p").unwrap();
    assert_eq!(body_ps.len(), 1);

    // The page name lives in <title> now, not in an <h1>.
    let titles = doc.get_elements_by_tag_name("title");
    assert_eq!(titles.len(), 1);
    assert_eq!(doc.text_content(titles[0]).trim(), "Example Domain");

    server.await.expect("server task");
}

#[test]
fn parse_example_com_snapshot_markup() {
    // Pins the parser against the real-world shape of that page: minified, with
    // UNQUOTED attribute values (lang=en, charset=utf-8, src=/s.js), a <style>
    // and a <script> in <head>. No network — the snapshot is the input.
    use agent_browser::html::parse;

    let html = include_str!("fixtures/14_example_com_snapshot.html");
    let doc = parse(html);

    assert_eq!(doc.get_elements_by_tag_name("html").len(), 1);
    assert_eq!(doc.get_elements_by_tag_name("head").len(), 1);
    assert_eq!(doc.get_elements_by_tag_name("body").len(), 1);
    assert_eq!(doc.get_elements_by_tag_name("title").len(), 1);
    assert_eq!(doc.get_elements_by_tag_name("p").len(), 1);
    assert_eq!(doc.get_elements_by_tag_name("script").len(), 1);
    assert_eq!(doc.get_elements_by_tag_name("style").len(), 1);
    assert_eq!(doc.get_elements_by_tag_name("meta").len(), 2);
    assert_eq!(doc.get_elements_by_tag_name("link").len(), 1);
    // No <h1> in today's markup — the parser must not invent one.
    assert_eq!(doc.get_elements_by_tag_name("h1").len(), 0);

    // The unquoted attribute value survived the parse.
    let scripts = doc.get_elements_by_tag_name("script");
    assert_eq!(doc.get_attribute(scripts[0], "src"), Some("/s.js"));

    let metas = doc.get_elements_by_tag_name("meta");
    assert_eq!(doc.get_attribute(metas[0], "charset"), Some("utf-8"));
}

#[test]
fn parse_misnested_formatting_adoption_agency() {
    // The adoption agency algorithm's own example, from the HTML Living
    // Standard §13.2.8.3. The markup is ill-formed on purpose: </b> closes an
    // element while an <i> is still open inside it. Browsers recover by
    // re-opening the <i> after the <b> closes, so the text that follows
    // </b> stays inside <i> instead of falling out of it.
    //
    //   <p>1<b>2<i>3</b>4</i>5</p>  =>  <p>1<b>2<i>3</i></b><i>4</i>5</p>
    //
    // A parser that just pops the stack to the matching tag loses the <i>:
    // it produces <p>1<b>2<i>3</i></b>45</p> and silently drops the italic
    // formatting from the rest of the paragraph.
    use agent_browser::html::parse;

    let html = include_str!("fixtures/15_misnested_formatting.html");
    let doc = parse(html);

    let paras = doc.get_elements_by_tag_name("p");
    assert_eq!(paras.len(), 2, "fixture should yield exactly two paragraphs");

    // First paragraph: the spec's canonical example.
    assert_eq!(
        doc.inner_html(paras[0]),
        "1<b>2<i>3</i></b><i>4</i>5",
        "misnested <i> must be re-opened after </b> (adoption agency, §13.2.8.3)"
    );

    // Second paragraph: the misnesting is one level deeper and the misnested
    // element carries the text after the stray end tag.
    //
    // The two expected trees below are not transcribed from the spec prose —
    // they were cross-checked against html5lib 1.1 (a spec-compliant HTML5
    // parsing implementation) on the same markup, which produces:
    //   <p>1<b>2<i>3</i></b><i>4</i>5</p>
    //   <p><b>bold <i>both</i></b><i> italic</i> plain</p>
    assert_eq!(
        doc.inner_html(paras[1]),
        "<b>bold <i>both</i></b><i> italic</i> plain",
        "misnested formatting elements must be reconstructed, not discarded"
    );

    // The reconstructed <i> keeps the count the browser would produce:
    // two <i> elements in the first paragraph (the original inside <b>, the
    // reconstructed one after it).
    let is = doc.get_elements_by_tag_name("i");
    assert_eq!(is.len(), 4, "two <i> per paragraph: original + reconstructed");
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

#[test]
fn parse_nested_anchor_special_case() {
    // The `<a>` special case (HTML Living Standard §13.2.6.4.7): a start tag
    // for `<a>` does not simply nest. If an `<a>` is still in the list of
    // active formatting elements, the new tag closes it first — a parse error
    // — and the two anchors end up as siblings:
    //
    //   <p><a href="a">1<a href="b">2</p>
    //     becomes
    //   <p><a href="a">1</a><a href="b">2</a></p>
    //
    // The expected tree was cross-checked against html5lib 1.1, which
    // produces exactly this tree — it was not transcribed from the spec prose
    // (last slice's first expectation was wrong, and the reference caught it).
    use agent_browser::html::parse;

    let html = include_str!("fixtures/16_nested_anchor.html");
    let doc = parse(html);

    let paras = doc.get_elements_by_tag_name("p");
    assert_eq!(paras.len(), 1, "fixture should yield exactly one paragraph");
    assert_eq!(
        doc.inner_html(paras[0]),
        "<a href=\"a\">1</a><a href=\"b\">2</a>",
        "the <a> special case must close the active anchor instead of nesting"
    );

    // The two anchors are siblings: the first holds only "1", not the second
    // anchor. A parser that nests them produces "1<a href=\"b\">2</a>".
    let anchors = doc.get_elements_by_tag_name("a");
    assert_eq!(anchors.len(), 2, "the fixture has two anchors");
    assert_eq!(doc.inner_html(anchors[0]), "1");
    assert_eq!(doc.inner_html(anchors[1]), "2");
}
