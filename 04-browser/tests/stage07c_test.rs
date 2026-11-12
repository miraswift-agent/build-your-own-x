//! Browser harden slice 7c: the adoption agency's parse errors.
//!
//! The expected codes were produced by **html5lib 1.1** on the same markup
//! (`HTMLParser().parse(...)`, then reading `parser.errors`) -- not
//! transcribed from the spec prose. My memory of the spec's error codes
//! has been wrong before, twice; the reference caught it both times.
//!
//! What html5lib records for the same markup:
//!
//! ```text
//! <p>1<b>2<i>3</b>4</i>5</p>   adoption-agency-1.3
//! <b>1<p>2</b>3</p>            adoption-agency-1.3
//! <b>1</b>                    (none)
//! <p><a href="x">1<a href="y">2</a>3</a></p>
//!                              unexpected-start-tag-implies-end-tag
//!                              unexpected-end-tag
//! ```

use agent_browser::html::parse;

/// The parse errors the adoption agency can produce, in order.
fn agency_errors(html: &str) -> Vec<String> {
    let doc = parse(html);
    doc.errors
        .iter()
        .filter(|e| {
            e.starts_with("adoption-agency")
                || e.starts_with("unexpected-end-tag")
                || e.starts_with("unexpected-start-tag-implies-end-tag")
        })
        .cloned()
        .collect()
}

#[test]
fn the_misnested_end_tag_records_the_spec_error() {
    let errors = agency_errors("<p>1<b>2<i>3</b>4</i>5</p>");
    assert_eq!(errors.len(), 1, "expected one agency error: {errors:?}");
    assert!(
        errors[0].starts_with("adoption-agency-1.3"),
        "expected adoption-agency-1.3: {errors:?}"
    );
}

#[test]
fn the_block_between_case_records_the_same_error() {
    let errors = agency_errors("<b>1<p>2</b>3</p>");
    assert_eq!(errors.len(), 1, "expected one agency error: {errors:?}");
    assert!(
        errors[0].starts_with("adoption-agency-1.3"),
        "expected adoption-agency-1.3: {errors:?}"
    );
}

#[test]
fn a_well_formed_end_tag_records_nothing() {
    let errors = agency_errors("<b>1</b>");
    assert!(errors.is_empty(), "expected no agency errors: {errors:?}");
}

#[test]
fn the_nested_anchor_records_both_of_its_errors() {
    let errors = agency_errors("<p><a href=\"x\">1<a href=\"y\">2</a>3</a></p>");
    assert_eq!(errors.len(), 2, "expected two agency errors: {errors:?}");
    assert!(
        errors[0].starts_with("unexpected-start-tag-implies-end-tag"),
        "expected the nested <a> error first: {errors:?}"
    );
    assert!(
        errors[1].starts_with("unexpected-end-tag"),
        "expected the unmatched </a> error second: {errors:?}"
    );
}

#[test]
fn the_table_case_records_the_unexpected_end_tag() {
    // A <table> start tag clears the list of active formatting elements,
    // so by the time the </b> arrives there is no formatting element left
    // and the adoption agency falls back to "any other end tag" -- which
    // walks off the top of the stack and finds a <td> in the way. html5lib
    // records exactly the same thing for this markup.
    let errors = agency_errors("<b>x<table><td>y</b>");
    assert_eq!(errors.len(), 1, "expected one agency error: {errors:?}");
    assert!(
        errors[0].starts_with("unexpected-end-tag"),
        "expected unexpected-end-tag: {errors:?}"
    );
}

#[test]
fn the_out_of_scope_case_records_the_specs_code() {
    // A <table> does NOT clear the list of active formatting elements, so
    // the <b> is still on the list when its end tag arrives -- but the
    // <table> is a scope boundary, so the <b> is not in scope. The spec
    // says: parse error, ignore the token.
    //
    // (html5lib clears the list here, because its table insertion mode
    // creates an implied <tbody>, and a <tbody> start tag does clear the
    // list. This component has no table insertion modes, so the <b>
    // survives -- and reaches the branch the spec describes.)
    let errors = agency_errors("<b>x<table>y</b>");
    assert_eq!(errors.len(), 1, "expected one agency error: {errors:?}");
    assert!(
        errors[0].starts_with("adoption-agency-4.4"),
        "expected adoption-agency-4.4: {errors:?}"
    );
}
