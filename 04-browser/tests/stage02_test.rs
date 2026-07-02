//! Stage 02 tests: DOM Tree, selector matching, mutation observers, enhanced accessibility.

use agent_browser::html::{parse, ElementCategory, ElementData, NodeData, SemanticRole};
use agent_browser::html::dom::{Attribute, MutationKind, DOCUMENT_NODE_ID};
use agent_browser::dom::{
    build_enhanced_access_tree, is_focusable_element, query_selector, query_selector_all,
    InteractiveKind, MutationInit, MutationObserver,
};

// ─── Helpers ──────────────────────────────────────────────────────────────────

fn make_element(tag: &str) -> NodeData {
    let (category, role) = agent_browser::html::dom::classify_element(tag, &[]);
    NodeData::Element(ElementData {
        tag_name: tag.to_string(),
        attrs: vec![],
        category,
        role,
    })
}

fn make_element_with_attrs(tag: &str, attrs: &[(&str, &str)]) -> NodeData {
    let attr_list: Vec<Attribute> = attrs.iter()
        .map(|(k, v)| Attribute::new(*k, *v))
        .collect();
    let (category, role) = agent_browser::html::dom::classify_element(tag, &attr_list);
    NodeData::Element(ElementData {
        tag_name: tag.to_string(),
        attrs: attr_list,
        category,
        role,
    })
}

// ─── Selector: type selectors ─────────────────────────────────────────────────

#[test]
fn test_select_type_selector() {
    let doc = parse("<html><body><div><p>Hello</p></div></body></html>");
    let result = query_selector(&doc, DOCUMENT_NODE_ID, "p").unwrap();
    assert!(result.is_some(), "should find <p>");
    assert_eq!(doc.node(result.unwrap()).tag_name(), Some("p"));
}

#[test]
fn test_select_type_selector_all() {
    let doc = parse("<div><p>A</p><p>B</p><p>C</p></div>");
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "p").unwrap();
    assert_eq!(results.len(), 3);
}

#[test]
fn test_select_universal() {
    let doc = parse("<div><p>A</p><span>B</span></div>");
    // Universal selector matches every element
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "*").unwrap();
    assert!(results.len() >= 4, "should find html, body, div, p, span");
}

// ─── Selector: class selectors ────────────────────────────────────────────────

#[test]
fn test_select_class_selector() {
    let doc = parse(r#"<div><p class="foo">A</p><p class="bar">B</p></div>"#);
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, ".foo").unwrap();
    assert_eq!(results.len(), 1);
    assert_eq!(doc.node(results[0]).tag_name(), Some("p"));
    assert_eq!(doc.get_attribute(results[0], "class"), Some("foo"));
}

#[test]
fn test_select_multiple_classes() {
    let doc = parse(r#"<div class="foo bar">X</div><div class="foo">Y</div>"#);
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, ".foo").unwrap();
    assert_eq!(results.len(), 2);
}

#[test]
fn test_select_compound_type_class() {
    let doc = parse(r#"<p class="intro">A</p><div class="intro">B</div>"#);
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "p.intro").unwrap();
    assert_eq!(results.len(), 1);
    assert_eq!(doc.node(results[0]).tag_name(), Some("p"));
}

// ─── Selector: ID selectors ───────────────────────────────────────────────────

#[test]
fn test_select_id_selector() {
    let doc = parse(r#"<div id="main"><p id="intro">Hello</p></div>"#);
    let result = query_selector(&doc, DOCUMENT_NODE_ID, "#intro").unwrap();
    assert!(result.is_some());
    assert_eq!(doc.node(result.unwrap()).tag_name(), Some("p"));
}

#[test]
fn test_select_id_not_found() {
    let doc = parse("<div><p>Hello</p></div>");
    let result = query_selector(&doc, DOCUMENT_NODE_ID, "#nonexistent").unwrap();
    assert!(result.is_none());
}

// ─── Selector: attribute selectors ───────────────────────────────────────────

#[test]
fn test_select_attr_exists() {
    let doc = parse(r#"<a href="/home">Home</a><a>No href</a>"#);
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "[href]").unwrap();
    assert_eq!(results.len(), 1);
}

#[test]
fn test_select_attr_exact() {
    let doc = parse(r#"<input type="text"><input type="checkbox"><input type="submit">"#);
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "[type=text]").unwrap();
    assert_eq!(results.len(), 1);
}

#[test]
fn test_select_attr_includes() {
    let doc = parse(r#"<div class="foo bar baz">X</div><div class="bar">Y</div>"#);
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "[class~=foo]").unwrap();
    assert_eq!(results.len(), 1);
}

#[test]
fn test_select_attr_prefix() {
    let doc = parse(r#"<a href="https://example.com">A</a><a href="http://other.com">B</a>"#);
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "[href^=https]").unwrap();
    assert_eq!(results.len(), 1);
}

#[test]
fn test_select_attr_suffix() {
    let doc = parse(r#"<img src="photo.jpg"><img src="logo.png">"#);
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "[src$=.jpg]").unwrap();
    assert_eq!(results.len(), 1);
}

#[test]
fn test_select_attr_substring() {
    let doc = parse(r#"<div data-component="nav-bar">X</div><div data-component="sidebar">Y</div>"#);
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "[data-component*=nav]").unwrap();
    assert_eq!(results.len(), 1);
}

#[test]
fn test_select_attr_dash_match() {
    let doc = parse(r#"<div lang="en-US">A</div><div lang="en-GB">B</div><div lang="fr">C</div>"#);
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "[lang|=en]").unwrap();
    assert_eq!(results.len(), 2);
}

// ─── Selector: combinators ────────────────────────────────────────────────────

#[test]
fn test_select_descendant_combinator() {
    let doc = parse("<div><section><p>Deep</p></section></div>");
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "div p").unwrap();
    assert_eq!(results.len(), 1);
    assert_eq!(doc.node(results[0]).tag_name(), Some("p"));
}

#[test]
fn test_select_child_combinator() {
    let doc = parse("<div><p>Direct</p><section><p>Nested</p></section></div>");
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "div > p").unwrap();
    // Only the direct child, not the nested one
    assert_eq!(results.len(), 1);
    let text = doc.text_content(results[0]);
    assert_eq!(text.trim(), "Direct");
}

#[test]
fn test_select_adjacent_sibling() {
    let doc = parse("<div><h2>Title</h2><p>First para</p><p>Second para</p></div>");
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "h2 + p").unwrap();
    assert_eq!(results.len(), 1);
    assert!(doc.text_content(results[0]).contains("First"));
}

#[test]
fn test_select_general_sibling() {
    let doc = parse("<div><h2>Title</h2><p>A</p><span>B</span><p>C</p></div>");
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "h2 ~ p").unwrap();
    assert_eq!(results.len(), 2, "should find both p siblings after h2");
}

// ─── Selector: pseudo-classes ─────────────────────────────────────────────────

#[test]
fn test_select_first_child() {
    let doc = parse("<ul><li>A</li><li>B</li><li>C</li></ul>");
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "li:first-child").unwrap();
    assert_eq!(results.len(), 1);
    assert_eq!(doc.text_content(results[0]).trim(), "A");
}

#[test]
fn test_select_last_child() {
    let doc = parse("<ul><li>A</li><li>B</li><li>C</li></ul>");
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "li:last-child").unwrap();
    assert_eq!(results.len(), 1);
    assert_eq!(doc.text_content(results[0]).trim(), "C");
}

#[test]
fn test_select_nth_child_index() {
    let doc = parse("<ul><li>A</li><li>B</li><li>C</li></ul>");
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "li:nth-child(2)").unwrap();
    assert_eq!(results.len(), 1);
    assert_eq!(doc.text_content(results[0]).trim(), "B");
}

#[test]
fn test_select_nth_child_even() {
    let doc = parse("<ul><li>1</li><li>2</li><li>3</li><li>4</li></ul>");
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "li:nth-child(even)").unwrap();
    assert_eq!(results.len(), 2);
}

#[test]
fn test_select_nth_child_odd() {
    let doc = parse("<ul><li>1</li><li>2</li><li>3</li><li>4</li></ul>");
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "li:nth-child(odd)").unwrap();
    assert_eq!(results.len(), 2);
}

#[test]
fn test_select_nth_child_formula() {
    let doc = parse("<ul><li>1</li><li>2</li><li>3</li><li>4</li><li>5</li><li>6</li></ul>");
    // 3n+1 = positions 1, 4
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "li:nth-child(3n+1)").unwrap();
    assert_eq!(results.len(), 2);
}

#[test]
fn test_select_empty_pseudo() {
    let doc = parse(r#"<div><p></p><p>Not empty</p><span>   </span></div>"#);
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, ":empty").unwrap();
    // <p></p> and <span>   </span> (whitespace-only) should match
    assert!(!results.is_empty());
}

#[test]
fn test_select_root_pseudo() {
    let doc = parse("<html><body><p>Hello</p></body></html>");
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, ":root").unwrap();
    assert_eq!(results.len(), 1);
    assert_eq!(doc.node(results[0]).tag_name(), Some("html"));
}

#[test]
fn test_select_not_pseudo() {
    let doc = parse(r#"<p class="foo">A</p><p class="bar">B</p><p>C</p>"#);
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "p:not(.foo)").unwrap();
    assert_eq!(results.len(), 2);
}

// ─── Selector: selector lists ─────────────────────────────────────────────────

#[test]
fn test_select_selector_list() {
    let doc = parse("<h1>A</h1><h2>B</h2><h3>C</h3><p>D</p>");
    let results = query_selector_all(&doc, DOCUMENT_NODE_ID, "h1, h2, h3").unwrap();
    assert_eq!(results.len(), 3);
}

// ─── DOM operations ───────────────────────────────────────────────────────────

#[test]
fn test_dom_get_element_by_id() {
    let doc = parse(r#"<div id="main"><p id="intro">Hello</p></div>"#);
    assert!(doc.get_element_by_id("main").is_some());
    assert!(doc.get_element_by_id("intro").is_some());
    assert!(doc.get_element_by_id("nope").is_none());
}

#[test]
fn test_dom_get_elements_by_class_name() {
    let doc = parse(r#"<p class="note">A</p><span class="note important">B</span><div>C</div>"#);
    let results = doc.get_elements_by_class_name("note");
    assert_eq!(results.len(), 2);
}

#[test]
fn test_dom_get_elements_by_tag_name() {
    let doc = parse("<ul><li>A</li><li>B</li><li>C</li></ul>");
    let results = doc.get_elements_by_tag_name("li");
    assert_eq!(results.len(), 3);
}

#[test]
fn test_dom_navigation() {
    let doc = parse("<div><p>A</p><span>B</span><em>C</em></div>");
    let div = doc.find_element("div").unwrap();
    let first = doc.first_child_of(div).unwrap();
    let last  = doc.last_child_of(div).unwrap();
    assert_ne!(first, last);
    // Navigate next sibling from first child
    let next = doc.next_sibling_of(first);
    assert!(next.is_some());
    // Navigate prev sibling from last child
    let prev = doc.previous_sibling_of(last);
    assert!(prev.is_some());
    // Parent of first child is div
    assert_eq!(doc.parent_of(first), Some(div));
}

#[test]
fn test_dom_get_set_attribute() {
    let mut doc = parse(r#"<div class="old">Hello</div>"#);
    let div = doc.find_element("div").unwrap();
    assert_eq!(doc.get_attribute(div, "class"), Some("old"));
    doc.set_attribute(div, "class", "new");
    assert_eq!(doc.get_attribute(div, "class"), Some("new"));
    doc.set_attribute(div, "id", "box");
    assert_eq!(doc.get_attribute(div, "id"), Some("box"));
    assert!(doc.has_attribute(div, "id"));
    doc.remove_attribute(div, "id");
    assert!(!doc.has_attribute(div, "id"));
}

#[test]
fn test_dom_class_list() {
    let mut doc = parse(r#"<div class="foo bar">Hello</div>"#);
    let div = doc.find_element("div").unwrap();
    let classes = doc.class_list(div);
    assert!(classes.contains(&"foo".to_string()));
    assert!(classes.contains(&"bar".to_string()));
    doc.class_list_add(div, "baz");
    assert!(doc.class_list_contains(div, "baz"));
    doc.class_list_remove(div, "foo");
    assert!(!doc.class_list_contains(div, "foo"));
    assert!(doc.class_list_contains(div, "bar"));
}

#[test]
fn test_dom_inner_html() {
    let doc = parse("<div><p>Hello</p><span>World</span></div>");
    let div = doc.find_element("div").unwrap();
    let inner = doc.inner_html(div);
    assert!(inner.contains("<p>"), "inner_html should contain <p>");
    assert!(inner.contains("Hello"), "inner_html should contain text");
    assert!(inner.contains("</p>"), "inner_html should contain </p>");
}

#[test]
fn test_dom_outer_html() {
    let doc = parse("<p class=\"note\">Hello</p>");
    let p = doc.find_element("p").unwrap();
    let outer = doc.outer_html(p);
    assert!(outer.starts_with("<p"), "outer_html should start with <p");
    assert!(outer.contains("note"), "outer_html should contain class attribute");
    assert!(outer.contains("Hello"), "outer_html should contain text");
    assert!(outer.ends_with("</p>"), "outer_html should end with </p>");
}

#[test]
fn test_dom_insert_child() {
    let mut doc = parse("<div></div>");
    let div = doc.find_element("div").unwrap();
    let child_id = doc.insert_child(div, NodeData::Text("hello".to_string()));
    assert_eq!(doc.text_content(div).trim(), "hello");
    assert!(doc.children_of(div).contains(&child_id));
}

#[test]
fn test_dom_remove_node() {
    let mut doc = parse("<div><p>Remove me</p><span>Keep me</span></div>");
    let div = doc.find_element("div").unwrap();
    let p   = doc.find_element("p").unwrap();
    doc.remove_node(p);
    assert!(!doc.children_of(div).contains(&p));
    assert!(!doc.text_content(div).contains("Remove me"));
}

#[test]
fn test_dom_insert_before() {
    let mut doc = parse("<div><span>Second</span></div>");
    let div  = doc.find_element("div").unwrap();
    let span = doc.find_element("span").unwrap();
    let new  = doc.insert_node_before(div, span, NodeData::Text("First".to_string()));
    let children = doc.children_of(div).to_vec();
    let new_pos  = children.iter().position(|&c| c == new).unwrap();
    let span_pos = children.iter().position(|&c| c == span).unwrap();
    assert!(new_pos < span_pos, "inserted node should come before ref node");
}

#[test]
fn test_dom_text_content_recursive() {
    let doc = parse("<div><p>Hello</p> <span>World</span></div>");
    let div = doc.find_element("div").unwrap();
    let text = doc.text_content(div);
    assert!(text.contains("Hello"));
    assert!(text.contains("World"));
}

// ─── Mutation observer ────────────────────────────────────────────────────────

#[test]
fn test_mutation_child_list_append() {
    let mut doc = parse("<div></div>");
    let div = doc.find_element("div").unwrap();

    let observer = MutationObserver::new(div, MutationInit::child_list());

    let _child = doc.insert_child(div, NodeData::Text("hello".into()));

    let records = observer.take_records(&mut doc);
    assert_eq!(records.len(), 1);
    assert_eq!(records[0].kind, MutationKind::ChildList);
    assert_eq!(records[0].target, div);
    assert!(!records[0].added_nodes.is_empty());
}

#[test]
fn test_mutation_child_list_remove() {
    let mut doc = parse("<div><p>Hello</p></div>");
    let div = doc.find_element("div").unwrap();
    let p   = doc.find_element("p").unwrap();

    let observer = MutationObserver::new(div, MutationInit::child_list());

    doc.remove_node(p);

    let records = observer.take_records(&mut doc);
    assert_eq!(records.len(), 1);
    assert_eq!(records[0].kind, MutationKind::ChildList);
    assert!(records[0].removed_nodes.contains(&p));
}

#[test]
fn test_mutation_attribute() {
    let mut doc = parse(r#"<div class="old"></div>"#);
    let div = doc.find_element("div").unwrap();

    let observer = MutationObserver::new(div, MutationInit::attributes());

    doc.set_attribute(div, "class", "new");

    let records = observer.take_records(&mut doc);
    assert_eq!(records.len(), 1);
    assert_eq!(records[0].kind, MutationKind::Attributes);
    assert_eq!(records[0].attribute_name.as_deref(), Some("class"));
    assert_eq!(records[0].old_value.as_deref(), Some("old"));
}

#[test]
fn test_mutation_attribute_filter() {
    let mut doc = parse(r#"<div class="old" id="box"></div>"#);
    let div = doc.find_element("div").unwrap();

    // Only watch 'id' changes
    let observer = MutationObserver::new(div, MutationInit {
        attributes: true,
        attribute_filter: vec!["id".to_string()],
        ..Default::default()
    });

    doc.set_attribute(div, "class", "new"); // not watched
    doc.set_attribute(div, "id", "container"); // watched

    let records = observer.take_records(&mut doc);
    assert_eq!(records.len(), 1, "only id change should be recorded");
    assert_eq!(records[0].attribute_name.as_deref(), Some("id"));
}

#[test]
fn test_mutation_subtree() {
    let mut doc = parse("<div><p></p></div>");
    let div = doc.find_element("div").unwrap();
    let p   = doc.find_element("p").unwrap();

    let observer = MutationObserver::new(div, MutationInit {
        child_list: true,
        subtree: true,
        ..Default::default()
    });

    // Mutation on p (descendant of div)
    doc.insert_child(p, NodeData::Text("hello".into()));

    let records = observer.take_records(&mut doc);
    assert_eq!(records.len(), 1, "subtree mutation should be observed");
    assert_eq!(records[0].target, p);
}

#[test]
fn test_mutation_disconnect() {
    let mut doc = parse("<div></div>");
    let div = doc.find_element("div").unwrap();

    let mut observer = MutationObserver::new(div, MutationInit::child_list());
    observer.disconnect();

    doc.insert_child(div, NodeData::Text("hello".into()));

    let records = observer.take_records(&mut doc);
    assert!(records.is_empty(), "disconnected observer should get no records");
}

#[test]
fn test_mutation_take_records_drains() {
    let mut doc = parse("<div></div>");
    let div = doc.find_element("div").unwrap();

    let observer = MutationObserver::new(div, MutationInit::child_list());

    doc.insert_child(div, NodeData::Text("a".into()));
    doc.insert_child(div, NodeData::Text("b".into()));

    let r1 = observer.take_records(&mut doc);
    assert_eq!(r1.len(), 2);
    // Second call should return nothing (already drained)
    let r2 = observer.take_records(&mut doc);
    assert!(r2.is_empty());
}

#[test]
fn test_mutation_character_data() {
    let mut doc = parse("<p>Old text</p>");
    let p       = doc.find_element("p").unwrap();
    // Find the text node child
    let text_node = doc.children_of(p).iter()
        .find(|&&c| doc.nodes[c].is_text())
        .copied()
        .expect("p should have a text node");

    let observer = MutationObserver::new(text_node, MutationInit {
        character_data: true,
        ..Default::default()
    });

    doc.set_text_content(text_node, "New text".to_string());

    let records = observer.take_records(&mut doc);
    assert_eq!(records.len(), 1);
    assert_eq!(records[0].kind, MutationKind::CharacterData);
    assert_eq!(records[0].old_value.as_deref(), Some("Old text"));
}

// ─── Enhanced accessibility tree ──────────────────────────────────────────────

#[test]
fn test_access_interactive_link() {
    let doc = parse(r#"<a href="/home">Home</a><a>No href</a>"#);
    let tree = build_enhanced_access_tree(&doc);
    let links: Vec<_> = tree.interactive.iter()
        .filter(|e| e.kind == InteractiveKind::Link)
        .collect();
    assert_eq!(links.len(), 1, "only <a> with href should be interactive link");
    assert_eq!(links[0].name, "Home");
}

#[test]
fn test_access_interactive_button() {
    let doc = parse(r#"<button>Click me</button>"#);
    let tree = build_enhanced_access_tree(&doc);
    let buttons: Vec<_> = tree.interactive.iter()
        .filter(|e| e.kind == InteractiveKind::Button)
        .collect();
    assert_eq!(buttons.len(), 1);
    assert_eq!(buttons[0].name, "Click me");
}

#[test]
fn test_access_interactive_input_types() {
    let doc = parse(r#"
        <form>
            <input type="text" placeholder="Name">
            <input type="password" placeholder="Pass">
            <input type="email" placeholder="Email">
            <input type="checkbox">
            <input type="radio">
            <input type="submit" value="Submit">
            <input type="hidden" name="token">
        </form>
    "#);
    let tree = build_enhanced_access_tree(&doc);
    let kinds: Vec<&InteractiveKind> = tree.interactive.iter().map(|e| &e.kind).collect();
    assert!(kinds.contains(&&InteractiveKind::TextInput));
    assert!(kinds.contains(&&InteractiveKind::PasswordInput));
    assert!(kinds.contains(&&InteractiveKind::EmailInput));
    assert!(kinds.contains(&&InteractiveKind::Checkbox));
    assert!(kinds.contains(&&InteractiveKind::Radio));
    assert!(kinds.contains(&&InteractiveKind::Submit));
    // hidden inputs should NOT be interactive
    assert!(!kinds.iter().any(|k| matches!(k, InteractiveKind::Other(s) if s.contains("hidden"))));
}

#[test]
fn test_access_focusable_detection() {
    let doc = parse(r#"
        <a href="/foo">Link</a>
        <button>Btn</button>
        <input type="text">
        <div>Not focusable</div>
    "#);
    let a   = doc.find_element("a").unwrap();
    let btn = doc.find_element("button").unwrap();
    let inp = doc.find_element("input").unwrap();
    let div = doc.find_element("div").unwrap();
    assert!( is_focusable_element(&doc, a),   "<a href> should be focusable");
    assert!( is_focusable_element(&doc, btn), "<button> should be focusable");
    assert!( is_focusable_element(&doc, inp), "<input> should be focusable");
    assert!(!is_focusable_element(&doc, div), "<div> should not be focusable");
}

#[test]
fn test_access_aria_label() {
    let doc = parse(r#"<button aria-label="Close dialog">X</button>"#);
    let tree = build_enhanced_access_tree(&doc);
    let btn = tree.interactive.iter().find(|e| e.kind == InteractiveKind::Button).unwrap();
    assert_eq!(btn.name, "Close dialog", "aria-label should override text content");
}

#[test]
fn test_access_aria_labelledby() {
    let doc = parse(r#"
        <label id="lbl">Your name</label>
        <input type="text" aria-labelledby="lbl">
    "#);
    let tree = build_enhanced_access_tree(&doc);
    let input = tree.interactive.iter().find(|e| e.kind == InteractiveKind::TextInput).unwrap();
    assert_eq!(input.name, "Your name", "aria-labelledby should resolve to label text");
}

#[test]
fn test_access_label_for() {
    let doc = parse(r#"
        <label for="name">Full Name</label>
        <input id="name" type="text">
    "#);
    let tree = build_enhanced_access_tree(&doc);
    let input = tree.interactive.iter().find(|e| e.kind == InteractiveKind::TextInput).unwrap();
    assert_eq!(input.name, "Full Name", "label[for] should name the input");
}

#[test]
fn test_access_disabled_element() {
    let doc = parse(r#"<button disabled>Can't click</button>"#);
    let tree = build_enhanced_access_tree(&doc);
    let btn = tree.interactive.iter().find(|e| e.kind == InteractiveKind::Button).unwrap();
    assert!(btn.is_disabled, "disabled button should be marked");
    assert!(!btn.is_focusable, "disabled button should not be focusable");
}

#[test]
fn test_access_id_map_populated() {
    let doc = parse(r#"<div id="header"><p id="intro">Hello</p></div>"#);
    let tree = build_enhanced_access_tree(&doc);
    assert!(tree.id_map.contains_key("header"));
    assert!(tree.id_map.contains_key("intro"));
    // Verify they point to the right nodes
    let header_id = tree.id_map["header"];
    assert_eq!(doc.get_attribute(header_id, "id"), Some("header"));
}

// ─── Integration: parse → query → observe ────────────────────────────────────

#[test]
fn test_integration_parse_query_observe() {
    let html = r#"
        <!DOCTYPE html>
        <html>
        <head><title>Test</title></head>
        <body>
          <nav id="nav">
            <a href="/">Home</a>
            <a href="/about">About</a>
          </nav>
          <main>
            <h1>Welcome</h1>
            <article class="post">
              <p class="lead">First paragraph.</p>
              <p>Second paragraph.</p>
            </article>
          </main>
        </body>
        </html>
    "#;
    let mut doc = parse(html);

    // Selector queries
    let h1 = query_selector(&doc, DOCUMENT_NODE_ID, "h1").unwrap().unwrap();
    assert_eq!(doc.text_content(h1).trim(), "Welcome");

    let links = query_selector_all(&doc, DOCUMENT_NODE_ID, "nav a[href]").unwrap();
    assert_eq!(links.len(), 2);

    let lead = query_selector(&doc, DOCUMENT_NODE_ID, "p.lead").unwrap().unwrap();
    assert!(doc.text_content(lead).contains("First"));

    let paras = query_selector_all(&doc, DOCUMENT_NODE_ID, "article > p").unwrap();
    assert_eq!(paras.len(), 2);

    // DOM mutation + observation
    let main = doc.find_element("main").unwrap();
    let observer = MutationObserver::new(main, MutationInit {
        child_list: true,
        subtree: true,
        attributes: true,
        ..Default::default()
    });

    let article = doc.find_element("article").unwrap();
    doc.set_attribute(article, "class", "post featured");
    doc.insert_child(main, NodeData::Text("Appended".into()));

    let records = observer.take_records(&mut doc);
    assert!(records.len() >= 2);
    let kinds: Vec<&MutationKind> = records.iter().map(|r| &r.kind).collect();
    assert!(kinds.contains(&&MutationKind::Attributes));
    assert!(kinds.contains(&&MutationKind::ChildList));

    // After mutation, query still works
    let featured = query_selector(&doc, DOCUMENT_NODE_ID, ".featured").unwrap();
    assert!(featured.is_some(), "newly added class should be queryable");

    // Accessibility tree
    let ax = build_enhanced_access_tree(&doc);
    assert!(!ax.base.headings.is_empty(), "should have headings");
    assert!(!ax.interactive.is_empty(), "should have interactive elements");
}

#[test]
fn test_integration_form_selectors() {
    let html = r#"
        <form action="/login" method="post">
            <label for="user">Username</label>
            <input id="user" type="text" name="username" required>
            <label for="pass">Password</label>
            <input id="pass" type="password" name="password" required>
            <button type="submit">Sign In</button>
        </form>
    "#;
    let doc = parse(html);

    // All required inputs
    let required = query_selector_all(&doc, DOCUMENT_NODE_ID, "[required]").unwrap();
    assert_eq!(required.len(), 2);

    // Text inputs
    let text_inputs = query_selector_all(&doc, DOCUMENT_NODE_ID, "input[type=text]").unwrap();
    assert_eq!(text_inputs.len(), 1);

    // Accessibility
    let ax = build_enhanced_access_tree(&doc);
    let text_input = ax.interactive.iter().find(|e| e.kind == InteractiveKind::TextInput);
    assert!(text_input.is_some());
    // Should be named via label[for]
    assert_eq!(text_input.unwrap().name, "Username");

    let pw = ax.interactive.iter().find(|e| e.kind == InteractiveKind::PasswordInput);
    assert!(pw.is_some());
    assert_eq!(pw.unwrap().name, "Password");
}

// ─── Existing Stage 01 tests still compile ────────────────────────────────────

#[test]
fn test_stage01_still_works() {
    // Just ensure the html module still works correctly after Stage 02 additions.
    use agent_browser::html::{build_access_tree, parse};

    let doc = parse("<h1>Title</h1><p>Body text</p>");
    let tree = build_access_tree(&doc);
    assert!(!tree.headings.is_empty());
    assert_eq!(tree.headings[0].1, "Title");
}
