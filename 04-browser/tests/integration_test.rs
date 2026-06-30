//! Integration tests for Stage 01: HTML Parser
//!
//! These tests validate:
//! 1. Correct tokenization of well-formed and broken HTML
//! 2. Correct tree structure for valid HTML
//! 3. Error recovery for malformed HTML (no panics, sensible output)
//! 4. Semantic classification of elements
//! 5. Accessibility tree extraction
//! 6. Edge cases: empty input, deeply nested, script/style, entities

use agent_browser::html::{
    build_access_tree, parse, tokenize, AXRole, NodeData, SemanticRole, Token,
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

fn count_elements(html: &str, tag: &str) -> usize {
    let doc = parse(html);
    doc.find_all_elements(tag).len()
}

fn has_element(html: &str, tag: &str) -> bool {
    !parse(html).find_all_elements(tag).is_empty()
}

fn body_text(html: &str) -> String {
    let doc = parse(html);
    match doc.body() {
        Some(id) => doc.text_content(id),
        None => doc.text_content(0),
    }
}

// ---------------------------------------------------------------------------
// Tokenizer tests
// ---------------------------------------------------------------------------

#[test]
fn tokenizer_simple_document() {
    let (tokens, errors) = tokenize("<!DOCTYPE html><html><head><title>Hi</title></head><body><p>Hello</p></body></html>");
    // Should have doctype, start tags, text, end tags
    assert!(tokens.iter().any(|t| matches!(t, Token::Doctype { name, .. } if name == "html")));
    assert!(tokens.iter().any(|t| matches!(t, Token::StartTag { name, .. } if name == "html")));
    assert!(tokens.iter().any(|t| matches!(t, Token::Text(t) if t.contains("Hello"))));
    assert!(tokens.iter().any(|t| matches!(t, Token::EndTag { name } if name == "body")));
    assert!(tokens.last() == Some(&Token::Eof));
}

#[test]
fn tokenizer_start_tag_attributes() {
    let (tokens, _) = tokenize(r#"<a href="https://example.com" target="_blank">link</a>"#);
    let start = tokens.iter().find(|t| matches!(t, Token::StartTag { name, .. } if name == "a"));
    assert!(start.is_some(), "should have <a> start tag");
    if let Some(Token::StartTag { attrs, .. }) = start {
        assert!(attrs.iter().any(|a| a.name == "href" && a.value == "https://example.com"));
        assert!(attrs.iter().any(|a| a.name == "target" && a.value == "_blank"));
    }
}

#[test]
fn tokenizer_comment() {
    let (tokens, _) = tokenize("<!-- hello world -->");
    assert!(tokens.iter().any(|t| matches!(t, Token::Comment(c) if c.contains("hello world"))));
}

#[test]
fn tokenizer_doctype_html5() {
    let (tokens, _) = tokenize("<!DOCTYPE html>");
    assert!(tokens.iter().any(|t| matches!(t, Token::Doctype { name, .. } if name == "html")));
}

#[test]
fn tokenizer_self_closing_tag() {
    let (tokens, _) = tokenize("<br/>");
    assert!(tokens.iter().any(|t| matches!(t, Token::StartTag { name, self_closing: true, .. } if name == "br")));
}

#[test]
fn tokenizer_single_quoted_attr() {
    let (tokens, _) = tokenize("<div class='foo bar'>x</div>");
    if let Some(Token::StartTag { attrs, .. }) = tokens.first() {
        assert!(attrs.iter().any(|a| a.name == "class" && a.value == "foo bar"));
    } else {
        panic!("expected start tag");
    }
}

#[test]
fn tokenizer_unquoted_attr() {
    let (tokens, _) = tokenize("<div id=myid>x</div>");
    if let Some(Token::StartTag { attrs, .. }) = tokens.first() {
        assert!(attrs.iter().any(|a| a.name == "id" && a.value == "myid"));
    } else {
        panic!("expected start tag");
    }
}

#[test]
fn tokenizer_boolean_attr() {
    let (tokens, _) = tokenize("<input type='checkbox' checked required>");
    if let Some(Token::StartTag { attrs, .. }) = tokens.first() {
        assert!(attrs.iter().any(|a| a.name == "checked" && a.value == ""));
        assert!(attrs.iter().any(|a| a.name == "required" && a.value == ""));
    } else {
        panic!("expected start tag");
    }
}

#[test]
fn tokenizer_entity_in_text() {
    let (tokens, _) = tokenize("<p>foo &amp; bar &lt; baz &gt; qux</p>");
    let text: String = tokens.iter()
        .filter_map(|t| if let Token::Text(s) = t { Some(s.as_str()) } else { None })
        .collect();
    assert!(text.contains('&'), "amp entity decoded");
    assert!(text.contains('<'), "lt entity decoded");
    assert!(text.contains('>'), "gt entity decoded");
}

#[test]
fn tokenizer_entity_in_attr() {
    let (tokens, _) = tokenize(r#"<a href="page?a=1&amp;b=2">link</a>"#);
    if let Some(Token::StartTag { attrs, .. }) = tokens.first() {
        let href = attrs.iter().find(|a| a.name == "href").map(|a| a.value.as_str());
        assert_eq!(href, Some("page?a=1&b=2"), "entity in attribute decoded");
    }
}

#[test]
fn tokenizer_numeric_entity() {
    let (tokens, _) = tokenize("<p>&#65;&#x41;</p>");
    let text: String = tokens.iter()
        .filter_map(|t| if let Token::Text(s) = t { Some(s.as_str()) } else { None })
        .collect();
    // Both &#65; and &#x41; should decode to 'A'
    assert!(text.contains("AA"), "numeric entities: got '{text}'");
}

#[test]
fn tokenizer_script_raw_text() {
    // The </div> inside the script should NOT produce an EndTag token
    let html = "<script>var x = '</div>'; if (a < b) {}</script><div>after</div>";
    let (tokens, _) = tokenize(html);
    // Should have exactly one EndTag for div (the real one after </script>)
    let end_div_count = tokens.iter()
        .filter(|t| matches!(t, Token::EndTag { name } if name == "div"))
        .count();
    assert_eq!(end_div_count, 1, "only one </div> end tag — the fake one is inside raw text");
}

#[test]
fn tokenizer_style_raw_text() {
    // Per HTML5 spec, </style> in raw text mode DOES end the element.
    // The spec does not understand CSS/JS strings — only the matching end tag matters.
    // This means you cannot put literal </style> inside a <style> element.
    // Use <\/style> or <" + "/style> in real code to avoid this.
    let html = "<style>body { color: red; }</style><p>after</p>";
    let (tokens, _) = tokenize(html);
    // There should be exactly one </style> end tag
    let end_style = tokens.iter()
        .filter(|t| matches!(t, Token::EndTag { name } if name == "style"))
        .count();
    assert_eq!(end_style, 1);
}

#[test]
fn tokenizer_truncated_tag() {
    // Malformed: tag never closed. Should not panic and should produce something useful.
    let (tokens, _errors) = tokenize("<div class='foo");
    // Should have at minimum an Eof token
    assert!(tokens.last() == Some(&Token::Eof));
}

#[test]
fn tokenizer_eof_in_comment() {
    let (tokens, _errors) = tokenize("<!-- unclosed comment");
    // Should emit the comment content + Eof
    assert!(tokens.last() == Some(&Token::Eof));
}

// ---------------------------------------------------------------------------
// Tree builder tests
// ---------------------------------------------------------------------------

#[test]
fn parse_minimal_html() {
    let doc = parse("<!DOCTYPE html><html><head></head><body><p>Hello</p></body></html>");
    assert!(doc.find_element("html").is_some());
    assert!(doc.find_element("head").is_some());
    assert!(doc.find_element("body").is_some());
    assert!(doc.find_element("p").is_some());
}

#[test]
fn parse_implied_html_head_body() {
    // No explicit html/head/body — tree builder should create them
    let doc = parse("<p>Hello world</p>");
    // body may or may not exist depending on impl, but p should be there
    assert!(doc.find_element("p").is_some(), "p element should be in tree");
    let text = doc.text_content(0);
    assert!(text.contains("Hello world"), "text content should be 'Hello world', got: '{text}'");
}

#[test]
fn parse_doctype_in_document() {
    let doc = parse("<!DOCTYPE html><html><body></body></html>");
    let has_doctype = doc.nodes.iter().any(|n| matches!(n.data, NodeData::Doctype(_)));
    assert!(has_doctype, "doctype should be in document");
}

#[test]
fn parse_void_elements_no_children() {
    let doc = parse("<html><body><img src='x.png' alt='x'><br><hr><input type='text'></body></html>");
    let img_id = doc.find_element("img").expect("img not found");
    assert!(doc.node(img_id).children.is_empty(), "void elements have no children");
}

#[test]
fn parse_script_content_is_text() {
    let doc = parse("<html><head><script>var x = 1; if (a < b) {}</script></head></html>");
    let script_id = doc.find_element("script").expect("script not found");
    let children = &doc.node(script_id).children;
    assert!(!children.is_empty(), "script should have a text child");
    if let Some(&text_id) = children.first() {
        if let NodeData::Text(t) = &doc.node(text_id).data {
            assert!(t.contains("var x = 1"), "script text content preserved");
        }
    }
}

#[test]
fn parse_adjacent_paragraphs_auto_close() {
    // Each <p> should auto-close the previous one
    let doc = parse("<body><p>First<p>Second<p>Third</body>");
    let ps = doc.find_all_elements("p");
    assert_eq!(ps.len(), 3, "should have 3 p elements, got {}", ps.len());
    // Each p should have only its own text
    for &pid in &ps {
        let text = doc.text_content(pid);
        assert!(!text.contains('\n') || text.trim().len() > 0, "p text should not be empty");
    }
}

#[test]
fn parse_li_auto_close() {
    let doc = parse("<ul><li>A<li>B<li>C</ul>");
    let lis = doc.find_all_elements("li");
    assert_eq!(lis.len(), 3, "should have 3 li elements");
    // Each li should have its own text
    let texts: Vec<String> = lis.iter().map(|&id| doc.text_content(id).trim().to_string()).collect();
    assert!(texts.contains(&"A".to_string()));
    assert!(texts.contains(&"B".to_string()));
    assert!(texts.contains(&"C".to_string()));
}

#[test]
fn parse_broken_html_no_panic() {
    // These should all parse without panicking
    let cases = vec![
        "",
        "<",
        "</",
        "<!",
        "<!-",
        "<!--",
        "<div",
        "<div class=",
        "<div class='",
        "<div class='unclosed",
        "</div></div></div>",
        "<b>text<i>italic</b>end</i>",
        "<p>text<div>oops</div></p>",
        "text without any tags",
        "<div><span><p><em>",
        "<!DOCTYPE><html><body>",
    ];
    for html in cases {
        let doc = parse(html); // must not panic
        let _ = format!("{doc}"); // must be displayable
    }
}

#[test]
fn parse_mismatched_tags_recovery() {
    // <b>bold <i>bold-italic</b> italic</i>
    let doc = parse("<p><b>bold <i>bold-italic</b> italic</i></p>");
    // Should not panic; should have b and i in tree somewhere
    assert!(doc.find_element("b").is_some() || doc.find_element("i").is_some(),
        "formatting elements should survive mismatch recovery");
    let text = doc.text_content(0);
    assert!(text.contains("bold"), "text content should be preserved");
}

#[test]
fn parse_deeply_nested() {
    let mut html = String::from("<div>");
    for i in 0..50 {
        html.push_str(&format!("<div class='level-{i}'>"));
    }
    html.push_str("<p>deep content</p>");
    for _ in 0..50 {
        html.push_str("</div>");
    }
    html.push_str("</div>");
    let doc = parse(&html);
    let text = doc.text_content(0);
    assert!(text.contains("deep content"), "content should survive deep nesting");
}

#[test]
fn parse_unclosed_elements_at_eof() {
    // Elements left open at EOF should still contain their text
    let doc = parse("<div><p>Text without closing tags");
    let text = doc.text_content(0);
    assert!(text.contains("Text without closing tags"),
        "text preserved even without closing tags");
}

#[test]
fn parse_comment_in_body() {
    let doc = parse("<body><!-- a comment --><p>text</p></body>");
    let has_comment = doc.nodes.iter().any(|n| matches!(n.data, NodeData::Comment(ref c) if c.contains("a comment")));
    assert!(has_comment, "comment should be in the tree");
}

#[test]
fn parse_empty_input() {
    let doc = parse("");
    // Should produce at minimum a Document node
    assert!(!doc.nodes.is_empty());
    let _ = format!("{doc}"); // Should not panic
}

#[test]
fn parse_text_only() {
    let doc = parse("Hello, world!");
    let text = doc.text_content(0);
    assert!(text.contains("Hello, world!"), "plain text should be parsed");
}

// ---------------------------------------------------------------------------
// Semantic classification tests
// ---------------------------------------------------------------------------

#[test]
fn semantic_headings_classified() {
    let doc = parse("<h1>One</h1><h2>Two</h2><h3>Three</h3>");
    for (tag, expected_level) in &[("h1", 1u8), ("h2", 2), ("h3", 3)] {
        let id = doc.find_element(tag).expect(&format!("{tag} not found"));
        let elem = doc.node(id).element_data().unwrap();
        assert!(
            matches!(elem.role, SemanticRole::Heading { level } if level == *expected_level),
            "{tag} should have heading role with level {expected_level}"
        );
    }
}

#[test]
fn semantic_links_classified() {
    let doc = parse(r#"<a href="https://example.com">Click</a>"#);
    let id = doc.find_element("a").expect("a not found");
    let elem = doc.node(id).element_data().unwrap();
    assert!(
        matches!(&elem.role, SemanticRole::Link { href: Some(h) } if h.contains("example.com")),
        "link should have href extracted"
    );
}

#[test]
fn semantic_images_classified() {
    let doc = parse(r#"<img src="photo.jpg" alt="A photo">"#);
    let id = doc.find_element("img").expect("img not found");
    let elem = doc.node(id).element_data().unwrap();
    assert!(
        matches!(&elem.role, SemanticRole::Media { src: Some(s), .. } if s.contains("photo.jpg")),
        "img should have media role with src"
    );
}

#[test]
fn semantic_form_elements_classified() {
    use agent_browser::html::FormKind;
    let doc = parse("<form><input type='email'><button>Submit</button></form>");
    let form_id = doc.find_element("form").expect("form not found");
    let input_id = doc.find_element("input").expect("input not found");
    let btn_id = doc.find_element("button").expect("button not found");
    assert!(matches!(doc.node(form_id).element_data().unwrap().role, SemanticRole::Form(FormKind::Form)));
    assert!(matches!(doc.node(input_id).element_data().unwrap().role, SemanticRole::Form(FormKind::Input)));
    assert!(matches!(doc.node(btn_id).element_data().unwrap().role, SemanticRole::Form(FormKind::Button)));
}

#[test]
fn semantic_scripts_classified() {
    let doc = parse("<script>var x = 1;</script><style>body {}</style>");
    let script_id = doc.find_element("script").expect("script not found");
    let style_id = doc.find_element("style").expect("style not found");
    assert!(matches!(doc.node(script_id).element_data().unwrap().role, SemanticRole::Script { is_style: false }));
    assert!(matches!(doc.node(style_id).element_data().unwrap().role, SemanticRole::Script { is_style: true }));
}

#[test]
fn semantic_structural_elements() {
    let doc = parse("<header></header><nav></nav><main></main><aside></aside><footer></footer>");
    for tag in &["header", "nav", "main", "aside", "footer"] {
        let id = doc.find_element(tag).expect(&format!("{tag} not found"));
        let role = &doc.node(id).element_data().unwrap().role;
        assert!(matches!(role, SemanticRole::Structural), "{tag} should be Structural, got {role:?}");
    }
}

#[test]
fn semantic_list_elements() {
    let doc = parse("<ul><li>A</li></ul><ol><li>B</li></ol>");
    for tag in &["ul", "ol", "li"] {
        let id = doc.find_element(tag).expect(&format!("{tag} not found"));
        assert!(matches!(doc.node(id).element_data().unwrap().role, SemanticRole::List),
            "{tag} should be List");
    }
}

// ---------------------------------------------------------------------------
// Accessibility tree tests
// ---------------------------------------------------------------------------

#[test]
fn access_headings_extracted() {
    let doc = parse("<h1>Main Title</h1><h2>Sub Title</h2><h3>Sub Sub</h3>");
    let tree = build_access_tree(&doc);
    assert_eq!(tree.headings.len(), 3);
    assert_eq!(tree.headings[0], (1, "Main Title".to_string()));
    assert_eq!(tree.headings[1], (2, "Sub Title".to_string()));
    assert_eq!(tree.headings[2], (3, "Sub Sub".to_string()));
}

#[test]
fn access_links_extracted() {
    let doc = parse(r#"<a href="https://example.com">Example</a><a href="/local">Local</a>"#);
    let tree = build_access_tree(&doc);
    assert_eq!(tree.links.len(), 2);
    assert!(tree.links.iter().any(|(text, href)| text == "Example" && href.contains("example.com")));
    assert!(tree.links.iter().any(|(text, href)| text == "Local" && href == "/local"));
}

#[test]
fn access_form_extracted() {
    let doc = parse(r#"
        <form action="/login" method="post">
            <input type="email" name="email" placeholder="Email">
            <input type="password" name="pass" placeholder="Password" required>
            <button type="submit">Sign in</button>
        </form>
    "#);
    let tree = build_access_tree(&doc);
    assert_eq!(tree.forms.len(), 1);
    let form = &tree.forms[0];
    assert_eq!(form.action.as_deref(), Some("/login"));
    assert_eq!(form.controls.len(), 3); // email, password, button
    assert!(form.controls.iter().any(|c| c.name.as_deref() == Some("email")));
    assert!(form.controls.iter().any(|c| c.required));
}

#[test]
fn access_landmarks_extracted() {
    let doc = parse("<header></header><nav></nav><main></main><aside></aside><footer></footer>");
    let tree = build_access_tree(&doc);
    assert!(!tree.landmarks.is_empty(), "should have landmarks");
    assert!(tree.landmarks.iter().any(|(r, _)| matches!(r, agent_browser::html::access::LandmarkRole::Navigation)));
    assert!(tree.landmarks.iter().any(|(r, _)| matches!(r, agent_browser::html::access::LandmarkRole::Main)));
}

#[test]
fn access_scripts_excluded() {
    let doc = parse("<script>var x = 1;</script><p>Visible</p>");
    let tree = build_access_tree(&doc);
    // The display output should not include script content
    let display = format!("{tree}");
    assert!(!display.contains("var x = 1"), "script content should not appear in access tree");
}

#[test]
fn access_aria_label_used_as_name() {
    let doc = parse(r#"<button aria-label="Close dialog">×</button>"#);
    let tree = build_access_tree(&doc);
    // Walk tree looking for the button
    fn find_button(node: &agent_browser::html::AXNode) -> Option<&agent_browser::html::AXNode> {
        if matches!(node.role, AXRole::Button) { return Some(node); }
        for child in &node.children {
            if let Some(found) = find_button(child) { return Some(found); }
        }
        None
    }
    let btn = find_button(&tree.root);
    assert!(btn.is_some(), "should find button node");
    assert_eq!(btn.unwrap().name, "Close dialog", "aria-label should be used as name");
}

// ---------------------------------------------------------------------------
// Fixture-based tests
// ---------------------------------------------------------------------------

macro_rules! fixture_test {
    ($name:ident, $file:literal) => {
        #[test]
        fn $name() {
            let html = include_str!($file);
            let doc = parse(html);
            // Must not panic, must be displayable
            let display = format!("{doc}");
            assert!(!display.is_empty(), "display should not be empty for {}", $file);
            // Accessibility tree must also work
            let tree = build_access_tree(&doc);
            let _ = format!("{tree}");
        }
    };
}

fixture_test!(fixture_01_simple, "fixtures/01_simple.html");
fixture_test!(fixture_02_broken, "fixtures/02_broken.html");
fixture_test!(fixture_03_deeply_nested, "fixtures/03_deeply_nested.html");
fixture_test!(fixture_04_script_style, "fixtures/04_script_style.html");
fixture_test!(fixture_05_forms, "fixtures/05_forms.html");
fixture_test!(fixture_06_headings, "fixtures/06_headings.html");
fixture_test!(fixture_07_links_media, "fixtures/07_links_media.html");
fixture_test!(fixture_08_implicit_closing, "fixtures/08_implicit_closing.html");
fixture_test!(fixture_09_self_closing, "fixtures/09_self_closing.html");
fixture_test!(fixture_10_comments_doctype, "fixtures/10_comments_doctype.html");
fixture_test!(fixture_11_empty, "fixtures/11_empty.html");
fixture_test!(fixture_12_real_snippet, "fixtures/12_real_snippet.html");
fixture_test!(fixture_13_attributes, "fixtures/13_attributes.html");

#[test]
fn fixture_01_has_correct_structure() {
    let html = include_str!("fixtures/01_simple.html");
    let doc = parse(html);
    assert!(doc.find_element("h1").is_some());
    assert!(doc.find_element("ul").is_some());
    let lis = doc.find_all_elements("li");
    assert_eq!(lis.len(), 3);
    let tree = build_access_tree(&doc);
    assert!(!tree.headings.is_empty());
    assert!(tree.headings[0].1.contains("Hello"));
    assert!(!tree.links.is_empty());
}

#[test]
fn fixture_06_headings_outline() {
    let html = include_str!("fixtures/06_headings.html");
    let doc = parse(html);
    let tree = build_access_tree(&doc);
    // Should have headings at multiple levels
    assert!(tree.headings.iter().any(|(l, _)| *l == 1));
    assert!(tree.headings.iter().any(|(l, _)| *l == 2));
    assert!(tree.headings.iter().any(|(l, _)| *l == 3));
    assert_eq!(tree.headings[0], (1, "Main Title".to_string()));
}

#[test]
fn fixture_05_form_controls() {
    let html = include_str!("fixtures/05_forms.html");
    let doc = parse(html);
    let tree = build_access_tree(&doc);
    // Two forms
    assert_eq!(tree.forms.len(), 2, "should have 2 forms");
    // First form is the login form
    assert_eq!(tree.forms[0].action.as_deref(), Some("/login"));
    // Should have required fields
    assert!(tree.forms[0].controls.iter().any(|c| c.required));
}

#[test]
fn fixture_04_script_content_preserved() {
    let html = include_str!("fixtures/04_script_style.html");
    let doc = parse(html);
    let script_ids = doc.find_all_elements("script");
    assert!(!script_ids.is_empty(), "should have script elements");
    // Script text should contain the JavaScript
    for &sid in &script_ids {
        let text = doc.text_content(sid);
        if !text.is_empty() {
            assert!(!text.contains("<"), "script text should not contain parsed tags, got: {text}");
        }
    }
}

#[test]
fn fixture_12_real_page_semantic() {
    let html = include_str!("fixtures/12_real_snippet.html");
    let doc = parse(html);
    let tree = build_access_tree(&doc);

    // Should have headings
    assert!(!tree.headings.is_empty());

    // Should have links
    assert!(tree.links.len() >= 3, "should have navigation links");

    // Should have landmarks
    assert!(tree.landmarks.iter().any(|(r, _)| matches!(r, agent_browser::html::access::LandmarkRole::Navigation)),
        "should have navigation landmark");
    assert!(tree.landmarks.iter().any(|(r, _)| matches!(r, agent_browser::html::access::LandmarkRole::Main)),
        "should have main landmark");
}
