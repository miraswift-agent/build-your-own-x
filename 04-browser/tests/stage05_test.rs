//! Stage 05 — Agent API tests.
//!
//! All tests work offline using `Page::from_html`.

use std::time::Duration;

use agent_browser::agent::{Action, ActionChain, Page};

// ─── Helpers ──────────────────────────────────────────────────────────────────

fn simple_page() -> Page {
    Page::from_html(
        r#"<!DOCTYPE html>
<html>
<head>
  <title>Test Page</title>
  <meta name="description" content="A test page">
  <meta property="og:title" content="OG Title">
  <link rel="canonical" href="https://example.com/page">
</head>
<body>
  <h1 id="main-heading" class="headline big">Hello World</h1>
  <p class="intro">Welcome to the test page.</p>
  <a href="/about">About</a>
  <a href="/contact">Contact Us</a>
  <a href="https://example.com">Home</a>
  <div hidden>Hidden content</div>
  <div style="display:none">Also hidden</div>
  <div style="visibility:hidden">Invisible</div>
  <form action="/submit" method="post">
    <input name="username" type="text" placeholder="Enter username" required>
    <input name="password" type="password">
    <input type="submit" value="Login">
  </form>
  <table>
    <thead><tr><th>Name</th><th>Score</th></tr></thead>
    <tbody>
      <tr><td>Alice</td><td>95</td></tr>
      <tr><td>Bob</td><td>87</td></tr>
    </tbody>
  </table>
</body>
</html>"#,
    )
}

// ─── Page API ─────────────────────────────────────────────────────────────────

#[test]
fn page_title() {
    let page = simple_page();
    assert_eq!(page.title(), "Test Page");
}

#[test]
fn page_title_empty_when_missing() {
    let page = Page::from_html("<html><body>no title</body></html>");
    assert_eq!(page.title(), "");
}

#[test]
fn page_url_none_for_from_html() {
    let page = simple_page();
    assert!(page.url().is_none());
}

#[test]
fn page_url_set_by_from_html_with_url() {
    let page = Page::from_html_with_url("<html></html>", "https://example.com");
    assert_eq!(page.url(), Some("https://example.com"));
}

#[test]
fn page_content_contains_html() {
    let page = simple_page();
    let content = page.content();
    assert!(content.contains("Hello World"), "content: {content}");
    assert!(content.contains("Test Page"), "content: {content}");
}

#[test]
fn page_screenshot_returns_text_representation() {
    let page = simple_page();
    let shot = page.screenshot();
    assert!(shot.contains("Hello World"), "screenshot: {shot}");
    assert!(shot.contains("Welcome"), "screenshot: {shot}");
    // Hidden elements should not appear
    assert!(
        !shot.contains("Hidden content"),
        "screenshot should omit hidden: {shot}"
    );
    assert!(
        !shot.contains("Also hidden"),
        "screenshot should omit display:none: {shot}"
    );
}

#[test]
fn page_screenshot_has_heading_prefix() {
    let page = simple_page();
    let shot = page.screenshot();
    assert!(
        shot.contains("# Hello World"),
        "h1 should be prefixed: {shot}"
    );
}

#[test]
fn page_screenshot_handles_deeply_nested_documents() {
    let depth = 5000;
    let mut html = String::new();
    for _ in 0..depth {
        html.push_str("<section>");
    }
    html.push_str("<h1>Nested title</h1><p>Nested body</p>");
    for _ in 0..depth {
        html.push_str("</section>");
    }

    let page = Page::from_html(&html);
    let shot = page.screenshot();
    assert!(shot.contains("# Nested title"), "screenshot: {shot}");
    assert!(shot.contains("Nested body"), "screenshot: {shot}");
}

#[test]
fn page_wait_for_selector_finds_existing() {
    let page = simple_page();
    let result = page.wait_for_selector("h1", Duration::from_millis(100));
    assert!(result.is_ok(), "should find <h1>: {result:?}");
}

#[test]
fn page_wait_for_selector_times_out() {
    let page = simple_page();
    let result = page.wait_for_selector("#nonexistent-xyz", Duration::from_millis(60));
    assert!(result.is_err(), "should time out for missing selector");
}

#[test]
fn page_history_navigation() {
    // from_html_with_url sets the current URL.
    let page = Page::from_html_with_url("<html><title>A</title></html>", "https://a.com");
    assert_eq!(page.url(), Some("https://a.com"));

    // from_html without URL gives None.
    let page2 = Page::from_html("<html></html>");
    assert!(page2.url().is_none());
}

// ─── Element API ──────────────────────────────────────────────────────────────

#[test]
fn element_text_content() {
    let page = simple_page();
    let el = page.query("h1").expect("h1 must exist");
    assert_eq!(el.text_content(), "Hello World");
}

#[test]
fn element_tag_name() {
    let page = simple_page();
    let el = page.query("h1").expect("h1 must exist");
    assert_eq!(el.tag_name(), Some("h1".to_string()));
}

#[test]
fn element_id() {
    let page = simple_page();
    let el = page.query("h1").expect("h1 must exist");
    assert_eq!(el.id(), Some("main-heading".to_string()));
}

#[test]
fn element_class_list() {
    let page = simple_page();
    let el = page.query("h1").expect("h1 must exist");
    let classes = el.class_list();
    assert!(
        classes.contains(&"headline".to_string()),
        "classes: {classes:?}"
    );
    assert!(classes.contains(&"big".to_string()), "classes: {classes:?}");
}

#[test]
fn element_get_attribute() {
    let page = simple_page();
    let el = page.query("a").expect("first <a> must exist");
    assert_eq!(el.get_attribute("href"), Some("/about".to_string()));
}

#[test]
fn element_set_attribute() {
    let page = simple_page();
    let el = page.query("h1").expect("h1 must exist");
    el.set_attribute("data-test", "hello");
    assert_eq!(el.get_attribute("data-test"), Some("hello".to_string()));
}

#[test]
fn element_inner_html() {
    let page = Page::from_html("<html><body><div><span>inner</span></div></body></html>");
    let el = page.query("div").expect("div must exist");
    let html = el.inner_html();
    assert!(html.contains("<span>"), "inner_html: {html}");
    assert!(html.contains("inner"), "inner_html: {html}");
}

#[test]
fn element_is_visible_true_for_normal() {
    let page = simple_page();
    let el = page.query("h1").expect("h1 must exist");
    assert!(el.is_visible());
}

#[test]
fn element_is_visible_false_for_display_none() {
    let page = simple_page();
    let el = page
        .query("[style=\"display:none\"]")
        .expect("display:none element must exist");
    assert!(!el.is_visible());
}

#[test]
fn element_is_visible_false_for_hidden_attr() {
    let page = simple_page();
    let el = page.query("[hidden]").expect("hidden element must exist");
    assert!(!el.is_visible());
}

#[test]
fn element_is_enabled_for_normal_input() {
    let page = simple_page();
    let el = page
        .query("input[name=username]")
        .expect("username input must exist");
    assert!(el.is_enabled());
}

#[test]
fn element_is_enabled_false_for_disabled() {
    let page = Page::from_html("<html><body><input name='x' disabled></body></html>");
    let el = page.query("input").expect("input must exist");
    assert!(!el.is_enabled());
}

#[test]
fn element_type_text_sets_value() {
    let page = simple_page();
    let el = page
        .query("input[name=username]")
        .expect("username input must exist");
    el.type_text("testuser");
    assert_eq!(el.get_attribute("value"), Some("testuser".to_string()));
}

#[test]
fn element_click_does_not_panic() {
    let page = simple_page();
    let el = page.query("a").expect("link must exist");
    el.click(); // just logs — must not panic
}

#[test]
fn element_query_selector_child() {
    let page = Page::from_html("<html><body><div><span id='s'>text</span></div></body></html>");
    let div = page.query("div").expect("div must exist");
    let span = div.query_selector("span").expect("span must exist");
    assert_eq!(span.id(), Some("s".to_string()));
}

#[test]
fn element_query_selector_all_children() {
    let page = Page::from_html("<html><body><ul><li>a</li><li>b</li><li>c</li></ul></body></html>");
    let ul = page.query("ul").expect("ul must exist");
    let items = ul.query_selector_all("li");
    assert_eq!(items.len(), 3);
}

// ─── Query API ────────────────────────────────────────────────────────────────

#[test]
fn query_returns_first_match() {
    let page = simple_page();
    let el = page.query("a").expect("first link");
    assert_eq!(el.get_attribute("href"), Some("/about".to_string()));
}

#[test]
fn query_returns_none_for_missing() {
    let page = simple_page();
    assert!(page.query("#does-not-exist").is_none());
}

#[test]
fn query_all_returns_all_matches() {
    let page = simple_page();
    let links = page.query_all("a");
    assert_eq!(links.len(), 3, "expected 3 links");
}

#[test]
fn query_role_heading() {
    let page = simple_page();
    let headings = page.query_role("heading");
    assert!(
        !headings.is_empty(),
        "should find headings by implicit role"
    );
    assert_eq!(headings[0].text_content(), "Hello World");
}

#[test]
fn query_role_explicit_aria() {
    let page = Page::from_html(r#"<html><body><div role="banner">Banner</div></body></html>"#);
    let els = page.query_role("banner");
    assert_eq!(els.len(), 1);
    assert_eq!(els[0].text_content(), "Banner");
}

#[test]
fn query_text_finds_elements_containing_text() {
    let page = simple_page();
    let els = page.query_text("Hello");
    assert!(!els.is_empty(), "should find element with 'Hello'");
}

#[test]
fn query_text_is_case_insensitive() {
    let page = simple_page();
    let els = page.query_text("hello world");
    assert!(!els.is_empty());
}

#[test]
fn query_input_by_name() {
    let page = simple_page();
    let el = page.query_input("username").expect("username input");
    assert_eq!(el.get_attribute("name"), Some("username".to_string()));
}

#[test]
fn query_input_by_placeholder() {
    let page = simple_page();
    let el = page
        .query_input("Enter username")
        .expect("input by placeholder");
    assert_eq!(el.get_attribute("name"), Some("username".to_string()));
}

#[test]
fn query_link_by_text() {
    let page = simple_page();
    let el = page.query_link("About").expect("About link");
    assert_eq!(el.get_attribute("href"), Some("/about".to_string()));
}

#[test]
fn query_link_by_href() {
    let page = simple_page();
    let el = page.query_link("/contact").expect("Contact link by href");
    assert!(el
        .get_attribute("href")
        .unwrap_or_default()
        .contains("contact"));
}

// ─── Action API ───────────────────────────────────────────────────────────────

#[test]
fn action_click_returns_ok_for_existing_element() {
    let mut page = simple_page();
    let result = page.execute(Action::Click("h1".to_string()));
    assert!(result.is_ok(), "click should succeed: {result:?}");
    assert!(result.unwrap().contains("click"));
}

#[test]
fn action_click_returns_err_for_missing_element() {
    let mut page = simple_page();
    let result = page.execute(Action::Click("#no-such-element".to_string()));
    assert!(result.is_err());
}

#[test]
fn action_type_sets_value_attribute() {
    let mut page = simple_page();
    let result = page.execute(Action::Type(
        "input[name=username]".to_string(),
        "agentuser".to_string(),
    ));
    assert!(result.is_ok(), "type should succeed: {result:?}");
    let el = page.query("input[name=username]").unwrap();
    assert_eq!(el.get_attribute("value"), Some("agentuser".to_string()));
}

#[test]
fn action_scroll_always_succeeds() {
    let mut page = simple_page();
    let result = page.execute(Action::Scroll(0, 200));
    assert!(result.is_ok());
    assert!(result.unwrap().contains("scroll"));
}

#[test]
fn action_hover_always_succeeds() {
    let mut page = simple_page();
    let result = page.execute(Action::Hover("h1".to_string()));
    assert!(result.is_ok());
}

#[test]
fn action_evaluate_always_succeeds() {
    let mut page = simple_page();
    let result = page.execute(Action::Evaluate("document.title".to_string()));
    assert!(result.is_ok());
}

#[test]
fn action_chain_executes_all() {
    let mut page = simple_page();
    let chain = ActionChain::new()
        .and_then(Action::Hover("h1".to_string()))
        .and_then(Action::Scroll(0, 100))
        .and_then(Action::Click("a".to_string()));
    let results = page.execute_chain(chain);
    assert!(results.is_ok(), "chain should succeed: {results:?}");
    assert_eq!(results.unwrap().len(), 3);
}

#[test]
fn action_chain_stops_on_first_error() {
    let mut page = simple_page();
    let chain = ActionChain::new()
        .and_then(Action::Click("#missing".to_string()))
        .and_then(Action::Scroll(0, 100)); // should never run
    let result = page.execute_chain(chain);
    assert!(result.is_err());
}

// ─── Data Extraction ─────────────────────────────────────────────────────────

#[test]
fn extract_links_returns_all_links() {
    let page = simple_page();
    let links = page.extract_links();
    assert_eq!(links.len(), 3, "expected 3 links: {links:?}");
    assert!(links.iter().any(|(t, h)| t == "About" && h == "/about"));
    assert!(links
        .iter()
        .any(|(t, h)| t == "Contact Us" && h == "/contact"));
}

#[test]
fn extract_table_returns_rows() {
    let page = simple_page();
    let rows = page.extract_table("table");
    // header row + 2 data rows
    assert_eq!(rows.len(), 3, "expected 3 rows: {rows:?}");
    assert_eq!(rows[0], vec!["Name", "Score"]);
    assert_eq!(rows[1], vec!["Alice", "95"]);
    assert_eq!(rows[2], vec!["Bob", "87"]);
}

#[test]
fn extract_table_empty_for_no_table() {
    let page = Page::from_html("<html><body><p>no table</p></body></html>");
    let rows = page.extract_table("table");
    assert!(rows.is_empty());
}

#[test]
fn extract_forms_returns_form_info() {
    let page = simple_page();
    let forms = page.extract_forms();
    assert_eq!(forms.len(), 1);
    let form = &forms[0];
    assert_eq!(form.action, Some("/submit".to_string()));
    assert_eq!(form.method, "post");
    // 3 inputs: username, password, submit
    assert_eq!(form.inputs.len(), 3, "inputs: {}", form.inputs.len());
}

#[test]
fn extract_forms_captures_input_fields() {
    let page = simple_page();
    let forms = page.extract_forms();
    let form = &forms[0];
    let names: Vec<_> = form
        .inputs
        .iter()
        .filter_map(|i| i.name.as_deref())
        .collect();
    assert!(names.contains(&"username"), "names: {names:?}");
    assert!(names.contains(&"password"), "names: {names:?}");
}

#[test]
fn extract_forms_captures_required_flag() {
    let page = simple_page();
    let forms = page.extract_forms();
    let form = &forms[0];
    let username = form
        .inputs
        .iter()
        .find(|i| i.name.as_deref() == Some("username"))
        .expect("username input");
    assert!(username.required);
}

#[test]
fn extract_metadata_title() {
    let page = simple_page();
    let meta = page.extract_metadata();
    assert_eq!(meta.title, "Test Page");
}

#[test]
fn extract_metadata_description() {
    let page = simple_page();
    let meta = page.extract_metadata();
    assert_eq!(meta.description, Some("A test page".to_string()));
}

#[test]
fn extract_metadata_og_title() {
    let page = simple_page();
    let meta = page.extract_metadata();
    assert_eq!(meta.og_title, Some("OG Title".to_string()));
}

#[test]
fn extract_metadata_canonical_url() {
    let page = simple_page();
    let meta = page.extract_metadata();
    assert_eq!(
        meta.canonical_url,
        Some("https://example.com/page".to_string())
    );
}

#[test]
fn extract_text_returns_visible_text() {
    let page = simple_page();
    let text = page.extract_text();
    assert!(text.contains("Hello World"), "text: {text}");
    assert!(text.contains("Welcome"), "text: {text}");
    // Hidden divs should not contribute
    assert!(!text.contains("Hidden content"), "text: {text}");
}

#[test]
fn extract_text_excludes_scripts() {
    let page =
        Page::from_html("<html><body><p>visible</p><script>var x = 1;</script></body></html>");
    let text = page.extract_text();
    assert!(text.contains("visible"));
    assert!(!text.contains("var x"), "script should be excluded: {text}");
}

#[test]
fn extract_structured_returns_json_with_title() {
    let page = simple_page();
    let val = page.extract_structured("full");
    assert_eq!(val["title"].as_str(), Some("Test Page"));
}

#[test]
fn extract_structured_includes_links() {
    let page = simple_page();
    let val = page.extract_structured("full");
    let links = val["links"].as_array().expect("links array");
    assert!(!links.is_empty());
}

#[test]
fn extract_structured_includes_tables() {
    let page = simple_page();
    let val = page.extract_structured("tables");
    let tables = val["tables"].as_array().expect("tables array");
    assert_eq!(tables.len(), 1, "one table expected");
}

// ─── Integration test ─────────────────────────────────────────────────────────

#[test]
fn integration_form_fill_and_verify() {
    let mut page = Page::from_html(
        r#"<!DOCTYPE html>
<html>
<head><title>Login</title></head>
<body>
  <form action="/login" method="post">
    <input id="email" name="email" type="email" placeholder="Email address" required>
    <input id="pass"  name="password" type="password">
    <button type="submit">Sign In</button>
  </form>
</body>
</html>"#,
    );

    // Query elements.
    let email_input = page.query_input("email").expect("email input");
    assert_eq!(email_input.tag_name(), Some("input".to_string()));
    assert!(email_input.is_enabled());
    assert!(email_input.is_visible());

    // Type via ActionChain.
    let chain = ActionChain::new()
        .and_then(Action::Type(
            "#email".to_string(),
            "agent@example.com".to_string(),
        ))
        .and_then(Action::Type("#pass".to_string(), "s3cr3t".to_string()))
        .and_then(Action::Click("button".to_string()));
    page.execute_chain(chain).expect("chain must succeed");

    // Verify values were set.
    let email_el = page.query("#email").expect("#email");
    assert_eq!(
        email_el.get_attribute("value"),
        Some("agent@example.com".to_string())
    );

    let pass_el = page.query("#pass").expect("#pass");
    assert_eq!(pass_el.get_attribute("value"), Some("s3cr3t".to_string()));

    // Extract form info.
    let forms = page.extract_forms();
    assert_eq!(forms.len(), 1);
    assert_eq!(forms[0].action, Some("/login".to_string()));
}

#[test]
fn integration_table_extraction() {
    let page = Page::from_html(
        r#"<!DOCTYPE html>
<html><body>
<table>
  <tr><th>Country</th><th>Capital</th><th>Population</th></tr>
  <tr><td>USA</td><td>Washington D.C.</td><td>331M</td></tr>
  <tr><td>UK</td><td>London</td><td>67M</td></tr>
  <tr><td>France</td><td>Paris</td><td>67M</td></tr>
</table>
</body></html>"#,
    );

    let rows = page.extract_table("table");
    assert_eq!(rows.len(), 4);
    assert_eq!(rows[0], vec!["Country", "Capital", "Population"]);
    assert_eq!(rows[1][0], "USA");
    assert_eq!(rows[3][1], "Paris");
}

#[test]
fn integration_screenshot_structure() {
    let page = Page::from_html(
        r#"<!DOCTYPE html>
<html>
<head><title>News</title></head>
<body>
  <h1>Breaking News</h1>
  <h2>Top Stories</h2>
  <p>An important event happened today.</p>
  <script>console.log("hidden");</script>
</body>
</html>"#,
    );

    let shot = page.screenshot();
    assert!(shot.contains("# Breaking News"), "screenshot: {shot}");
    assert!(shot.contains("## Top Stories"), "screenshot: {shot}");
    assert!(shot.contains("important event"), "screenshot: {shot}");
    assert!(
        !shot.contains("console.log"),
        "script should be excluded: {shot}"
    );
}
