//! Browser harden slice 7b-1: the adoption agency's inner loop -- the oracle.
//!
//! Slice 7a wired the OUTER loop into the tree, but the inner step is still
//! this component's simplified re-opening, so the misnested trees can still
//! differ from html5lib's. This file does not implement the inner loop; it
//! MEASURES the gap, so the port that does can be verified case by case
//! instead of by memory.
//!
//! The expected trees were produced by **html5lib 1.1** on the same markup --
//! not transcribed from the spec prose. My memory of the spec's output has
//! been wrong before (twice), and the reference caught it both times.
//!
//! Two normalizations, both about the serializers rather than the parsers:
//! html5lib omits optional end tags (`</p>`, `</td>`, ...) and leaves
//! attribute values unquoted; this component always writes them. The
//! comparison is on the shape both agree on.

use agent_browser::html::parse;

/// The named cases: one fixture file, one document per case.
const FIXTURE: &str = include_str!("fixtures/17_inner_loop.html");

/// (name, html5lib 1.1's tree for the case's body).
const CASES: &[(&str, &str)] = &[
    ("misnested_b_i", "<p>1<b>2<i>3</i></b><i>4</i>5"),
    ("misnested_deeper", "<p><b>bold <i>both</i></b><i> italic</i> plain"),
    ("block_between", "<b>1</b><p><b>2</b>3"),
    ("across_block", "<b><div>x</div></b>"),
    ("anchor_special_case", "<p><a href=\"x\">1</a><a href=\"y\">2</a>3"),
    ("nested_same", "<b>1<i>2<b>3</b></i><b>4</b></b>"),
];

/// The cases whose trees differ from html5lib's TODAY. This is the work list
/// for the inner-loop port: when a case is fixed, this list changes and this
/// test fails, which is the point.
const KNOWN_GAPS: &[&str] = &["block_between"];

fn parse_case(html: &str) -> String {
    let doc = parse(html);
    let body = doc.get_elements_by_tag_name("body")[0];
    normalize(&doc.inner_html(body))
}

/// Drop the optional end tags html5lib's serializer omits.
fn normalize(s: &str) -> String {
    let mut out = s.to_string();
    for tag in [
        "p", "li", "td", "th", "tr", "tbody", "thead", "tfoot", "option", "dt", "dd", "rt", "rp",
        "optgroup",
    ] {
        out = out.replace(&format!("</{}>", tag), "");
    }
    out
}

fn cases_from_fixture() -> Vec<(&'static str, &'static str)> {
    let mut out = Vec::new();
    for chunk in FIXTURE.split("\n@@ ").skip(1) {
        let (name, html) = chunk.split_once('\n').expect("case has a name and a body");
        out.push((name, html.trim()));
    }
    out
}

#[test]
fn the_inner_loop_gap_is_exactly_the_known_set() {
    let cases = cases_from_fixture();
    assert_eq!(cases.len(), CASES.len(), "fixture/case count mismatch");

    let mut gaps: Vec<&str> = Vec::new();
    let mut mismatches: Vec<String> = Vec::new();
    for ((name, html), (case_name, expected)) in cases.iter().zip(CASES.iter()) {
        assert_eq!(name, case_name, "fixture order must match CASES");
        let ours = parse_case(html);
        let expected = normalize(expected);
        if ours != expected {
            gaps.push(name);
            mismatches.push(format!(
                "  {}:\n    ours:     {}\n    html5lib: {}",
                name, ours, expected
            ));
        }
    }
    assert_eq!(
        gaps, KNOWN_GAPS,
        "the inner-loop gap changed -- one of these cases was fixed (or broke):\n{}",
        mismatches.join("\n")
    );
}
