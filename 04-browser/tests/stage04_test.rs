//! Stage 04 tests: JavaScript Bridge & CDP
//!
//! Tests JS context creation/isolation, script evaluation, console capture,
//! exception handling, CDP session lifecycle, and WebSocket connectivity.

use agent_browser::js::{
    cdp::CdpServer,
    context::{JsValue, LogLevel, WorldType},
    engine::{JsContext, JsEngine, QuickJsEngine},
    security::{ContentSecurityPolicy, Origin},
};

// ─── JS Engine: context creation ────────────────────────────────────────────

#[test]
fn test_js_engine_create_context() {
    let engine = QuickJsEngine::new();
    let _ctx = engine.create_context().expect("should create a JS context");
}

#[test]
fn test_js_engine_create_utility_context() {
    let engine = QuickJsEngine::new();
    let ctx = engine.create_utility_context().expect("should create utility context");
    assert_eq!(ctx.world(), WorldType::Utility);
}

#[test]
fn test_js_context_ids_unique() {
    let engine = QuickJsEngine::new();
    let c1 = engine.create_context().unwrap();
    let c2 = engine.create_context().unwrap();
    assert_ne!(c1.context_id(), c2.context_id());
}

// ─── JS Engine: evaluation ───────────────────────────────────────────────────

#[test]
fn test_eval_number() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    let val = ctx.eval("1 + 1").unwrap();
    assert_eq!(val.as_number(), Some(2.0));
}

#[test]
fn test_eval_string() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    let val = ctx.eval("'hello' + ' world'").unwrap();
    assert_eq!(val.as_str(), Some("hello world"));
}

#[test]
fn test_eval_bool_true() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    let val = ctx.eval("1 === 1").unwrap();
    assert_eq!(val.as_bool(), Some(true));
}

#[test]
fn test_eval_bool_false() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    let val = ctx.eval("1 === 2").unwrap();
    assert_eq!(val.as_bool(), Some(false));
}

#[test]
fn test_eval_null() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    let val = ctx.eval("null").unwrap();
    assert!(val.is_null());
}

#[test]
fn test_eval_undefined() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    let val = ctx.eval("undefined").unwrap();
    assert!(val.is_undefined());
}

#[test]
fn test_eval_array() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    let val = ctx.eval("[1, 2, 3]").unwrap();
    match val {
        JsValue::Array(arr) => {
            assert_eq!(arr.len(), 3);
            assert_eq!(arr[0].as_number(), Some(1.0));
            assert_eq!(arr[2].as_number(), Some(3.0));
        }
        _ => panic!("expected array, got {val:?}"),
    }
}

#[test]
fn test_eval_object() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    let val = ctx.eval("({a: 1})").unwrap();
    assert!(matches!(val, JsValue::Object(_)));
}

#[test]
fn test_eval_function_declaration() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    // Declaring then calling a function
    ctx.eval("function add(a,b){return a+b;}").unwrap();
    let val = ctx.eval("add(3, 4)").unwrap();
    assert_eq!(val.as_number(), Some(7.0));
}

#[test]
fn test_eval_multiline_script() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    let script = "var x = 10;\nvar y = 20;\nx + y;";
    let val = ctx.eval(script).unwrap();
    assert_eq!(val.as_number(), Some(30.0));
}

#[test]
fn test_eval_persistent_state_between_calls() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    ctx.eval("var counter = 0;").unwrap();
    ctx.eval("counter += 1;").unwrap();
    ctx.eval("counter += 1;").unwrap();
    let val = ctx.eval("counter").unwrap();
    assert_eq!(val.as_number(), Some(2.0));
}

// ─── Context isolation ───────────────────────────────────────────────────────

#[test]
fn test_context_isolation_variable() {
    let engine = QuickJsEngine::new();
    let mut ctx1 = engine.create_context().unwrap();
    let mut ctx2 = engine.create_context().unwrap();

    ctx1.eval("var secret = 42;").unwrap();
    let val = ctx2.eval("typeof secret === 'undefined'").unwrap();
    assert_eq!(val.as_bool(), Some(true), "ctx2 must not see ctx1's variable");
}

#[test]
fn test_context_isolation_mutation() {
    let engine = QuickJsEngine::new();
    let mut ctx1 = engine.create_context().unwrap();
    let mut ctx2 = engine.create_context().unwrap();

    ctx1.eval("Array.prototype.evil = 99;").unwrap();
    let val = ctx2.eval("[].evil === undefined").unwrap();
    assert_eq!(val.as_bool(), Some(true), "prototype mutations must not cross contexts");
}

// ─── Console API ─────────────────────────────────────────────────────────────

#[test]
fn test_console_log_captured() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    ctx.eval("console.log('hello');").unwrap();
    let logs = ctx.take_console_logs();
    assert_eq!(logs.len(), 1);
    assert_eq!(logs[0].level, LogLevel::Log);
    assert_eq!(logs[0].message, "hello");
}

#[test]
fn test_console_error_captured() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    ctx.eval("console.error('boom');").unwrap();
    let logs = ctx.take_console_logs();
    assert_eq!(logs.len(), 1);
    assert_eq!(logs[0].level, LogLevel::Error);
    assert_eq!(logs[0].message, "boom");
}

#[test]
fn test_console_warn_captured() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    ctx.eval("console.warn('careful');").unwrap();
    let logs = ctx.take_console_logs();
    assert_eq!(logs.len(), 1);
    assert_eq!(logs[0].level, LogLevel::Warn);
}

#[test]
fn test_console_info_captured() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    ctx.eval("console.info('fyi');").unwrap();
    let logs = ctx.take_console_logs();
    assert_eq!(logs.len(), 1);
    assert_eq!(logs[0].level, LogLevel::Info);
}

#[test]
fn test_console_multiple_args_joined() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    ctx.eval("console.log('a', 'b', 'c');").unwrap();
    let logs = ctx.take_console_logs();
    assert_eq!(logs[0].message, "a b c");
}

#[test]
fn test_console_number_arg() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    ctx.eval("console.log(42);").unwrap();
    let logs = ctx.take_console_logs();
    assert_eq!(logs[0].message, "42");
}

#[test]
fn test_console_take_clears_buffer() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    ctx.eval("console.log('x');").unwrap();
    let _first = ctx.take_console_logs();
    let second = ctx.take_console_logs();
    assert!(second.is_empty(), "take_console_logs must drain the buffer");
}

#[test]
fn test_console_multiple_calls() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    ctx.eval("console.log('a'); console.log('b'); console.log('c');").unwrap();
    let logs = ctx.take_console_logs();
    assert_eq!(logs.len(), 3);
    assert_eq!(logs[0].message, "a");
    assert_eq!(logs[1].message, "b");
    assert_eq!(logs[2].message, "c");
}

// ─── Exception handling ───────────────────────────────────────────────────────

#[test]
fn test_exception_message() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    let err = ctx.eval("throw new Error('test error')").unwrap_err();
    assert!(err.message.contains("test error"), "message: {:?}", err.message);
}

#[test]
fn test_exception_syntax_error() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    let err = ctx.eval("{{{{not valid js").unwrap_err();
    assert!(!err.message.is_empty());
}

#[test]
fn test_exception_type_error() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    let err = ctx.eval("null.property").unwrap_err();
    assert!(!err.message.is_empty());
}

#[test]
fn test_exception_has_stack() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    let err = ctx.eval("function boom() { throw new Error('fail'); } boom();").unwrap_err();
    // Stack trace is optional in QuickJS but message must be present
    assert!(!err.message.is_empty());
}

#[test]
fn test_exception_string_throw() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    let err = ctx.eval("throw 'custom string error'").unwrap_err();
    assert!(err.message.contains("custom string error"));
}

#[test]
fn test_exception_does_not_poison_context() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    ctx.eval("throw new Error('oops')").unwrap_err();
    // Context should still be usable after an exception
    let val = ctx.eval("1 + 1").unwrap();
    assert_eq!(val.as_number(), Some(2.0));
}

// ─── setTimeout stubs ─────────────────────────────────────────────────────────

#[test]
fn test_set_timeout_returns_id() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    let val = ctx.eval("setTimeout(function(){}, 1000)").unwrap();
    assert!(matches!(val, JsValue::Number(_)), "setTimeout should return a timer ID");
}

#[test]
fn test_set_interval_returns_id() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    let val = ctx.eval("setInterval(function(){}, 500)").unwrap();
    assert!(matches!(val, JsValue::Number(_)));
}

#[test]
fn test_clear_timeout_no_crash() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();
    ctx.eval("clearTimeout(1)").unwrap();
    ctx.eval("clearInterval(2)").unwrap();
}

// ─── JsValue helpers ──────────────────────────────────────────────────────────

#[test]
fn test_jsvalue_display_number() {
    assert_eq!(JsValue::Number(42.0).to_string(), "42");
    assert_eq!(JsValue::Number(3.14).to_string(), "3.14");
}

#[test]
fn test_jsvalue_display_string() {
    assert_eq!(JsValue::String("hello".into()).to_string(), "hello");
}

#[test]
fn test_jsvalue_to_json_number() {
    let j = JsValue::Number(5.0).to_json();
    assert_eq!(j, serde_json::json!(5));
}

#[test]
fn test_jsvalue_to_json_string() {
    let j = JsValue::String("hi".into()).to_json();
    assert_eq!(j, serde_json::json!("hi"));
}

#[test]
fn test_jsvalue_type_str() {
    assert_eq!(JsValue::Number(1.0).type_str(), "number");
    assert_eq!(JsValue::String("x".into()).type_str(), "string");
    assert_eq!(JsValue::Bool(true).type_str(), "boolean");
    assert_eq!(JsValue::Null.type_str(), "object");
    assert_eq!(JsValue::Undefined.type_str(), "undefined");
}

// ─── CDP target/session model ─────────────────────────────────────────────────

#[tokio::test]
async fn test_cdp_create_target() {
    let server = CdpServer::bind("127.0.0.1:0").await.unwrap();
    let tid = server.create_target("https://example.com");
    assert!(!tid.is_empty());
    let targets = server.targets();
    let found = targets.iter().any(|t| t.target_id == tid);
    assert!(found, "created target must appear in target list");
}

#[tokio::test]
async fn test_cdp_get_targets_not_empty() {
    let server = CdpServer::bind("127.0.0.1:0").await.unwrap();
    let targets = server.targets();
    // CdpServer creates a default about:blank target on startup
    assert!(!targets.is_empty());
}

#[tokio::test]
async fn test_cdp_attach_session() {
    let server = CdpServer::bind("127.0.0.1:0").await.unwrap();
    let tid = server.create_target("about:blank");
    let sid = server.attach(&tid).expect("should attach to target");
    assert!(!sid.is_empty());
}

#[tokio::test]
async fn test_cdp_detach_session() {
    let server = CdpServer::bind("127.0.0.1:0").await.unwrap();
    let tid = server.create_target("about:blank");
    let sid = server.attach(&tid).unwrap();
    let detached = server.detach(&sid);
    assert!(detached, "detach should succeed");
}

#[tokio::test]
async fn test_cdp_detach_unknown_session() {
    let server = CdpServer::bind("127.0.0.1:0").await.unwrap();
    let detached = server.detach("nonexistent-session-id");
    assert!(!detached);
}

#[tokio::test]
async fn test_cdp_evaluate_in_session() {
    let server = CdpServer::bind("127.0.0.1:0").await.unwrap();
    let tid = server.create_target("about:blank");
    let sid = server.attach(&tid).unwrap();

    let result = server.evaluate_in_session(&sid, "2 + 2").unwrap().unwrap();
    assert_eq!(result.as_number(), Some(4.0));
}

#[tokio::test]
async fn test_cdp_evaluate_in_target() {
    let server = CdpServer::bind("127.0.0.1:0").await.unwrap();
    let tid = server.create_target("about:blank");

    let result = server.evaluate_in_target(&tid, "40 + 2").unwrap().unwrap();
    assert_eq!(result.as_number(), Some(42.0));
}

#[tokio::test]
async fn test_cdp_evaluate_exception_in_session() {
    let server = CdpServer::bind("127.0.0.1:0").await.unwrap();
    let tid = server.create_target("about:blank");
    let sid = server.attach(&tid).unwrap();

    let err = server.evaluate_in_session(&sid, "throw new Error('cdp error')").unwrap().unwrap_err();
    assert!(err.message.contains("cdp error"));
}

#[tokio::test]
async fn test_cdp_session_state_persists() {
    // Variables set in one eval should persist to the next in the same session
    let server = CdpServer::bind("127.0.0.1:0").await.unwrap();
    let tid = server.create_target("about:blank");
    let sid = server.attach(&tid).unwrap();

    server.evaluate_in_session(&sid, "var x = 100;").unwrap().unwrap();
    let val = server.evaluate_in_session(&sid, "x").unwrap().unwrap();
    assert_eq!(val.as_number(), Some(100.0));
}

#[tokio::test]
async fn test_cdp_targets_are_isolated() {
    // JS state in one target must not leak to another
    let server = CdpServer::bind("127.0.0.1:0").await.unwrap();
    let t1 = server.create_target("about:blank");
    let t2 = server.create_target("about:blank");

    server.evaluate_in_target(&t1, "var secret = 99;").unwrap().unwrap();
    let val = server.evaluate_in_target(&t2, "typeof secret === 'undefined'").unwrap().unwrap();
    assert_eq!(val.as_bool(), Some(true), "targets must be isolated");
}

// ─── CDP WebSocket protocol ───────────────────────────────────────────────────

#[tokio::test]
async fn test_cdp_websocket_connects() {
    let server = CdpServer::bind("127.0.0.1:0").await.unwrap();
    let port = server.port;
    tokio::spawn(server.run());

    // Give server a moment to start
    tokio::time::sleep(tokio::time::Duration::from_millis(20)).await;

    let url = format!("ws://127.0.0.1:{port}");
    let (mut ws, _) = tokio_tungstenite::connect_async(&url).await
        .expect("WebSocket connect should succeed");

    ws.close(None).await.ok();
}

#[tokio::test]
async fn test_cdp_websocket_get_targets() {
    use futures_util::{SinkExt, StreamExt};
    use tokio_tungstenite::tungstenite::Message;

    let server = CdpServer::bind("127.0.0.1:0").await.unwrap();
    let port = server.port;
    tokio::spawn(server.run());
    tokio::time::sleep(tokio::time::Duration::from_millis(20)).await;

    let url = format!("ws://127.0.0.1:{port}");
    let (mut ws, _) = tokio_tungstenite::connect_async(&url).await.unwrap();

    let req = serde_json::json!({ "id": 1, "method": "Target.getTargets", "params": {} });
    ws.send(Message::Text(req.to_string().into())).await.unwrap();

    let msg = ws.next().await.unwrap().unwrap();
    let resp: serde_json::Value = serde_json::from_str(msg.to_text().unwrap()).unwrap();

    assert_eq!(resp["id"], 1);
    assert!(resp["result"]["targetInfos"].is_array(), "should have targetInfos array");
    ws.close(None).await.ok();
}

#[tokio::test]
async fn test_cdp_websocket_evaluate_via_session() {
    use futures_util::{SinkExt, StreamExt};
    use tokio_tungstenite::tungstenite::Message;

    let server = CdpServer::bind("127.0.0.1:0").await.unwrap();
    let port = server.port;
    // Create a target and attach a session before starting server loop
    let tid = server.create_target("about:blank");
    tokio::spawn(server.run());
    tokio::time::sleep(tokio::time::Duration::from_millis(20)).await;

    let url = format!("ws://127.0.0.1:{port}");
    let (mut ws, _) = tokio_tungstenite::connect_async(&url).await.unwrap();

    // attachToTarget
    let attach_req = serde_json::json!({
        "id": 1,
        "method": "Target.attachToTarget",
        "params": { "targetId": tid }
    });
    ws.send(Message::Text(attach_req.to_string().into())).await.unwrap();

    // Read until we get the response to id=1 (events may come first)
    let session_id = loop {
        let msg = ws.next().await.unwrap().unwrap();
        let v: serde_json::Value = serde_json::from_str(msg.to_text().unwrap()).unwrap();
        if v.get("id") == Some(&serde_json::json!(1)) {
            break v["result"]["sessionId"].as_str().unwrap().to_string();
        }
    };

    // Runtime.evaluate in the session
    let eval_req = serde_json::json!({
        "id": 2,
        "sessionId": session_id,
        "method": "Runtime.evaluate",
        "params": { "expression": "6 * 7", "returnByValue": true }
    });
    ws.send(Message::Text(eval_req.to_string().into())).await.unwrap();

    let result_val = loop {
        let msg = ws.next().await.unwrap().unwrap();
        let v: serde_json::Value = serde_json::from_str(msg.to_text().unwrap()).unwrap();
        if v.get("id") == Some(&serde_json::json!(2)) {
            break v["result"]["result"]["value"].clone();
        }
    };

    assert_eq!(result_val, serde_json::json!(42));
    ws.close(None).await.ok();
}

// ─── Same-origin policy ───────────────────────────────────────────────────────

#[test]
fn test_origin_same() {
    let a = Origin::from_url("https://example.com/page").unwrap();
    let b = Origin::from_url("https://example.com/other").unwrap();
    assert!(a.is_same_origin(&b));
}

#[test]
fn test_origin_cross_scheme() {
    let a = Origin::from_url("http://example.com").unwrap();
    let b = Origin::from_url("https://example.com").unwrap();
    assert!(!a.is_same_origin(&b));
}

#[test]
fn test_origin_cross_host() {
    let a = Origin::from_url("https://foo.example.com").unwrap();
    let b = Origin::from_url("https://bar.example.com").unwrap();
    assert!(!a.is_same_origin(&b));
}

#[test]
fn test_origin_cross_port() {
    let a = Origin::from_url("https://example.com:8443").unwrap();
    let b = Origin::from_url("https://example.com").unwrap();
    assert!(!a.is_same_origin(&b));
}

#[test]
fn test_origin_null() {
    let null = Origin::null();
    let a = Origin::from_url("https://example.com").unwrap();
    assert!(!null.is_same_origin(&a));
    assert!(!a.is_same_origin(&null));
}

// ─── CSP parsing ──────────────────────────────────────────────────────────────

#[test]
fn test_csp_default_blocks_inline() {
    let csp = ContentSecurityPolicy::from_header("script-src 'self'");
    assert!(csp.blocks_inline());
    assert!(csp.blocks_eval());
}

#[test]
fn test_csp_unsafe_inline_allows_inline() {
    let csp = ContentSecurityPolicy::from_header("script-src 'self' 'unsafe-inline'");
    assert!(!csp.blocks_inline());
}

#[test]
fn test_csp_unsafe_eval_allows_eval() {
    let csp = ContentSecurityPolicy::from_header("script-src 'self' 'unsafe-eval'");
    assert!(!csp.blocks_eval());
}

#[test]
fn test_csp_none_blocks_all() {
    let csp = ContentSecurityPolicy::from_header("script-src 'none'");
    assert!(csp.blocks_inline());
    assert!(csp.blocks_eval());
}

#[test]
fn test_csp_origin_allowed() {
    let csp = ContentSecurityPolicy::from_header(
        "script-src 'self' https://cdn.example.com",
    );
    assert!(csp.allows_origin("https://cdn.example.com"));
    assert!(!csp.allows_origin("https://evil.com"));
}

// ─── Integration: fetch + parse + exec JS ────────────────────────────────────

#[test]
fn test_integration_parse_then_exec_js() {
    use agent_browser::html::parse;

    let html = r#"<!DOCTYPE html>
<html>
  <head><title>Test Page</title></head>
  <body>
    <h1 id="main">Hello World</h1>
    <p class="content">Some text here.</p>
  </body>
</html>"#;

    let doc = parse(html);

    // Verify DOM was parsed
    assert!(!doc.errors.is_empty() || doc.node(1).tag_name().is_some());

    // Now run JS in a separate context (DOM integration is future work;
    // here we verify the JS engine can reason about page content passed in as data)
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();

    ctx.eval("var pageTitle = 'Test Page';").unwrap();
    let val = ctx.eval("pageTitle.length").unwrap();
    assert_eq!(val.as_number(), Some(9.0));
}

#[test]
fn test_integration_js_dom_mutation_simulation() {
    // Simulate what a browser would do: parse HTML, run JS that modifies state
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();

    // Simulate a simple "DOM" as a JS object
    ctx.eval(r#"
        var document = {
            title: 'Original',
            body: { innerHTML: '<p>Hello</p>' }
        };
    "#).unwrap();

    ctx.eval("document.title = 'Modified by JS';").unwrap();

    let val = ctx.eval("document.title").unwrap();
    assert_eq!(val.as_str(), Some("Modified by JS"));
}

#[test]
fn test_integration_js_with_console_and_computation() {
    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap();

    ctx.eval(r#"
        function processItems(items) {
            var total = 0;
            for (var i = 0; i < items.length; i++) {
                total += items[i];
                console.log('Processing item: ' + items[i]);
            }
            return total;
        }
        var result = processItems([10, 20, 30]);
    "#).unwrap();

    let val = ctx.eval("result").unwrap();
    assert_eq!(val.as_number(), Some(60.0));

    let logs = ctx.take_console_logs();
    assert_eq!(logs.len(), 3);
    assert_eq!(logs[0].message, "Processing item: 10");
    assert_eq!(logs[2].message, "Processing item: 30");
}
