//! Chrome DevTools Protocol (CDP) server.
//!
//! Each TCP connection is handled individually; the first bytes determine
//! whether the client is doing a plain HTTP GET (for /json/* endpoints) or a
//! WebSocket upgrade. CDP messages are multiplexed on the WebSocket via
//! `sessionId`.

use std::collections::HashMap;
use std::sync::{Arc, Mutex};

use futures_util::{SinkExt, StreamExt};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value as Json};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::mpsc;
use tokio_tungstenite::tungstenite::Message;

use crate::js::context::{ContextId, JsError, JsValue, WorldType};
use crate::js::engine::{JsContext, JsEngine, QuickJsContext, QuickJsEngine};

// ─── Public types ────────────────────────────────────────────────────────────

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TargetInfo {
    #[serde(rename = "targetId")]
    pub target_id: String,
    #[serde(rename = "type")]
    pub target_type: String,
    pub url: String,
    pub title: String,
    pub description: String,
    pub attached: bool,
}

/// A CDP message (request, response, or event).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CdpMsg {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub id: Option<i64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub method: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub params: Option<Json>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub result: Option<Json>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<Json>,
    #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
    pub session_id: Option<String>,
}

impl CdpMsg {
    fn ok(id: i64, result: Json) -> Self {
        Self {
            id: Some(id), method: None, params: None,
            result: Some(result), error: None, session_id: None,
        }
    }

    fn err(id: i64, code: i32, message: &str) -> Self {
        Self {
            id: Some(id), method: None, params: None, result: None,
            error: Some(json!({ "code": code, "message": message })),
            session_id: None,
        }
    }

    fn event(method: &str, params: Json) -> Self {
        Self {
            id: None, method: Some(method.into()), params: Some(params),
            result: None, error: None, session_id: None,
        }
    }

    fn with_session(mut self, session_id: Option<&str>) -> Self {
        self.session_id = session_id.map(String::from);
        self
    }

    fn to_text(&self) -> String {
        serde_json::to_string(self).unwrap_or_default()
    }
}

// ─── Browser state ────────────────────────────────────────────────────────────

struct PageTarget {
    info: TargetInfo,
    context: Box<dyn JsContext + Send>,
    runtime_enabled: bool,
    console_enabled: bool,
    page_enabled: bool,
    network_enabled: bool,
}

struct BrowserState {
    targets: HashMap<String, PageTarget>,
    /// session_id → target_id
    sessions: HashMap<String, String>,
    port: u16,
}

impl BrowserState {
    fn new(port: u16) -> Self {
        Self { targets: HashMap::new(), sessions: HashMap::new(), port }
    }

    fn create_target(&mut self, url: &str) -> String {
        let target_id = uuid::Uuid::new_v4().to_string();
        let context: Box<dyn JsContext + Send> =
            QuickJsEngine::new().create_context().unwrap_or_else(|_| {
                Box::new(QuickJsContext::new(ContextId::next(), WorldType::Main).unwrap())
            });

        self.targets.insert(
            target_id.clone(),
            PageTarget {
                info: TargetInfo {
                    target_id: target_id.clone(),
                    target_type: "page".into(),
                    url: url.to_string(),
                    title: url.to_string(),
                    description: String::new(),
                    attached: false,
                },
                context,
                runtime_enabled: false,
                console_enabled: false,
                page_enabled: false,
                network_enabled: false,
            },
        );
        target_id
    }

    fn attach_session(&mut self, target_id: &str) -> Option<String> {
        self.targets.get(target_id)?;
        let session_id = uuid::Uuid::new_v4().to_string();
        if let Some(t) = self.targets.get_mut(target_id) {
            t.info.attached = true;
        }
        self.sessions.insert(session_id.clone(), target_id.to_string());
        Some(session_id)
    }

    fn detach_session(&mut self, session_id: &str) -> bool {
        if let Some(target_id) = self.sessions.remove(session_id) {
            let still_attached = self.sessions.values().any(|tid| tid == &target_id);
            if !still_attached {
                if let Some(t) = self.targets.get_mut(&target_id) {
                    t.info.attached = false;
                }
            }
            true
        } else {
            false
        }
    }

    fn target_info_list(&self) -> Vec<TargetInfo> {
        self.targets.values().map(|t| t.info.clone()).collect()
    }

    fn handle(&mut self, msg: &CdpMsg) -> Vec<CdpMsg> {
        let id = msg.id.unwrap_or(0);
        let method = msg.method.as_deref().unwrap_or("");
        let params = msg.params.as_ref();
        let session_id = msg.session_id.as_deref();
        let mut out: Vec<CdpMsg> = Vec::new();

        if let Some(sid) = session_id {
            let target_id = match self.sessions.get(sid).cloned() {
                Some(t) => t,
                None => {
                    out.push(CdpMsg::err(id, -32001, "Session not found").with_session(Some(sid)));
                    return out;
                }
            };
            let target = match self.targets.get_mut(&target_id) {
                Some(t) => t,
                None => {
                    out.push(CdpMsg::err(id, -32001, "Target not found").with_session(Some(sid)));
                    return out;
                }
            };
            let mut resps = handle_session_message(id, method, params, target);
            for r in &mut resps {
                r.session_id = Some(sid.to_string());
            }
            out.extend(resps);
            return out;
        }

        let resp = match method {
            "Target.getTargets" => {
                CdpMsg::ok(id, json!({ "targetInfos": self.target_info_list() }))
            }
            "Target.getTargetInfo" => {
                let tid = params
                    .and_then(|p| p.get("targetId"))
                    .and_then(|v| v.as_str())
                    .unwrap_or("");
                match self.targets.get(tid) {
                    Some(t) => CdpMsg::ok(id, json!({ "targetInfo": t.info })),
                    None => CdpMsg::err(id, -32001, "No such target"),
                }
            }
            "Target.createTarget" => {
                let url = params
                    .and_then(|p| p.get("url"))
                    .and_then(|v| v.as_str())
                    .unwrap_or("about:blank");
                let tid = self.create_target(url);
                let event = CdpMsg::event(
                    "Target.targetCreated",
                    json!({ "targetInfo": self.targets[&tid].info }),
                );
                out.push(event);
                CdpMsg::ok(id, json!({ "targetId": tid }))
            }
            "Target.attachToTarget" => {
                let tid = params
                    .and_then(|p| p.get("targetId"))
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                match self.attach_session(&tid) {
                    Some(sid) => {
                        let info = self.targets[&tid].info.clone();
                        out.push(CdpMsg::event(
                            "Target.attachedToTarget",
                            json!({
                                "sessionId": sid,
                                "targetInfo": info,
                                "waitingForDebugger": false
                            }),
                        ));
                        CdpMsg::ok(id, json!({ "sessionId": sid }))
                    }
                    None => CdpMsg::err(id, -32001, "No such target"),
                }
            }
            "Target.detachFromTarget" => {
                let sid = params
                    .and_then(|p| p.get("sessionId"))
                    .and_then(|v| v.as_str())
                    .unwrap_or("");
                if self.detach_session(sid) {
                    CdpMsg::ok(id, json!({}))
                } else {
                    CdpMsg::err(id, -32001, "Session not found")
                }
            }
            "Browser.getVersion" => CdpMsg::ok(
                id,
                json!({
                    "protocolVersion": "1.3",
                    "product": "agent-browser/0.4.0",
                    "revision": "stage04",
                    "userAgent": "agent-browser/0.4.0",
                    "jsVersion": "QuickJS"
                }),
            ),
            "Target.setDiscoverTargets" | "Target.setAutoAttach" => {
                CdpMsg::ok(id, json!({}))
            }
            unknown => CdpMsg::err(id, -32601, &format!("Method not found: {unknown}")),
        };

        out.push(resp);
        out
    }
}

fn handle_session_message(
    id: i64,
    method: &str,
    params: Option<&Json>,
    target: &mut PageTarget,
) -> Vec<CdpMsg> {
    let mut out = Vec::new();

    let resp = match method {
        "Runtime.enable" => { target.runtime_enabled = true; CdpMsg::ok(id, json!({})) }
        "Runtime.disable" => { target.runtime_enabled = false; CdpMsg::ok(id, json!({})) }

        "Runtime.evaluate" => {
            let expression = params
                .and_then(|p| p.get("expression")).and_then(|v| v.as_str()).unwrap_or("");
            let return_by_value = params
                .and_then(|p| p.get("returnByValue")).and_then(|v| v.as_bool()).unwrap_or(false);

            match target.context.eval(expression) {
                Ok(val) => {
                    let remote = js_value_to_remote_object(&val, return_by_value);
                    for log in target.context.take_console_logs() {
                        if target.console_enabled {
                            out.push(CdpMsg::event("Console.messageAdded", json!({
                                "message": {
                                    "source": "console-api",
                                    "level": log.level.as_str(),
                                    "text": log.message,
                                    "url": log.url,
                                    "line": log.line,
                                    "column": log.column
                                }
                            })));
                        }
                    }
                    CdpMsg::ok(id, json!({ "result": remote }))
                }
                Err(e) => CdpMsg::ok(id, json!({
                    "result": { "type": "undefined" },
                    "exceptionDetails": {
                        "exceptionId": 1,
                        "text": e.message,
                        "lineNumber": e.line.unwrap_or(0),
                        "columnNumber": e.column.unwrap_or(0),
                        "exception": {
                            "type": "object",
                            "subtype": "error",
                            "description": e.message
                        }
                    }
                })),
            }
        }

        "Runtime.callFunctionOn" => {
            let func_decl = params
                .and_then(|p| p.get("functionDeclaration")).and_then(|v| v.as_str())
                .unwrap_or("function(){}");
            let script = format!("({func_decl})()");
            match target.context.eval(&script) {
                Ok(val) => CdpMsg::ok(id, json!({ "result": js_value_to_remote_object(&val, false) })),
                Err(e) => CdpMsg::ok(id, json!({
                    "result": { "type": "undefined" },
                    "exceptionDetails": { "text": e.message }
                })),
            }
        }

        "Runtime.getProperties" => CdpMsg::ok(id, json!({ "result": [] })),

        "Console.enable" => { target.console_enabled = true; CdpMsg::ok(id, json!({})) }
        "Console.disable" => { target.console_enabled = false; CdpMsg::ok(id, json!({})) }

        "Page.enable" => { target.page_enabled = true; CdpMsg::ok(id, json!({})) }
        "Page.disable" => { target.page_enabled = false; CdpMsg::ok(id, json!({})) }

        "Page.navigate" => {
            let url = params
                .and_then(|p| p.get("url")).and_then(|v| v.as_str()).unwrap_or("about:blank");
            let frame_id = uuid::Uuid::new_v4().to_string();
            target.info.url = url.to_string();
            target.info.title = url.to_string();
            if target.page_enabled {
                out.push(CdpMsg::event("Page.frameNavigated", json!({
                    "frame": {
                        "id": frame_id, "url": url,
                        "securityOrigin": url, "mimeType": "text/html"
                    },
                    "type": "Navigation"
                })));
            }
            CdpMsg::ok(id, json!({ "frameId": frame_id, "loaderId": uuid::Uuid::new_v4().to_string() }))
        }

        "Network.enable" => { target.network_enabled = true; CdpMsg::ok(id, json!({})) }
        "Network.disable" => { target.network_enabled = false; CdpMsg::ok(id, json!({})) }

        unknown => CdpMsg::err(id, -32601, &format!("Method not found: {unknown}")),
    };

    out.push(resp);
    out
}

fn js_value_to_remote_object(val: &JsValue, return_by_value: bool) -> Json {
    let mut obj = serde_json::Map::new();
    obj.insert("type".into(), Json::String(val.type_str().into()));
    if let Some(sub) = val.subtype_str() {
        obj.insert("subtype".into(), Json::String(sub.into()));
    }
    if return_by_value
        || matches!(val, JsValue::Bool(_) | JsValue::Number(_) | JsValue::String(_)
                       | JsValue::Null | JsValue::Undefined)
    {
        obj.insert("value".into(), val.to_json());
    }
    let desc = match val {
        JsValue::Undefined => "undefined".into(),
        JsValue::Null => "null".into(),
        JsValue::Bool(b) => b.to_string(),
        JsValue::Number(n) => n.to_string(),
        JsValue::String(s) => s.clone(),
        JsValue::Object(_) => "Object".into(),
        JsValue::Array(a) => format!("Array({})", a.len()),
        JsValue::Function => "function".into(),
    };
    obj.insert("description".into(), Json::String(desc));
    Json::Object(obj)
}

// ─── HTTP helpers ─────────────────────────────────────────────────────────────

fn http_json_response(body: &str) -> String {
    format!(
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}",
        body.len()
    )
}

// ─── TCP connection handler ───────────────────────────────────────────────────

async fn read_headers(stream: &mut TcpStream) -> Vec<u8> {
    let mut buf = Vec::with_capacity(1024);
    let mut one = [0u8; 1];
    loop {
        match stream.read(&mut one).await {
            Ok(0) | Err(_) => break,
            Ok(_) => {
                buf.push(one[0]);
                if buf.ends_with(b"\r\n\r\n") { break; }
                if buf.len() > 8192 { break; }
            }
        }
    }
    buf
}

async fn handle_tcp(stream: TcpStream, state: Arc<Mutex<BrowserState>>) {
    let mut stream = stream;
    let header_bytes = read_headers(&mut stream).await;
    let headers = String::from_utf8_lossy(&header_bytes);
    let is_ws = headers.to_lowercase().contains("upgrade: websocket");

    if is_ws {
        handle_websocket(PeekedStream::new(stream, header_bytes), state).await;
    } else {
        handle_http(stream, &headers, state).await;
    }
}

async fn handle_http(mut stream: TcpStream, headers: &str, state: Arc<Mutex<BrowserState>>) {
    let first_line = headers.lines().next().unwrap_or("");
    let path = first_line.split_whitespace().nth(1).unwrap_or("/");
    let port = state.lock().map(|s| s.port).unwrap_or(9222);

    let body = match path {
        "/json/version" | "/json/version/" => {
            json!({
                "Browser": "agent-browser/0.4.0",
                "Protocol-Version": "1.3",
                "webSocketDebuggerUrl": format!("ws://127.0.0.1:{port}"),
                "V8-Version": "QuickJS"
            }).to_string()
        }
        "/json" | "/json/" | "/json/list" | "/json/list/" => {
            let targets: Vec<Json> = state.lock().map(|s| {
                s.target_info_list().into_iter().map(|t| {
                    json!({
                        "id": t.target_id,
                        "title": t.title,
                        "type": t.target_type,
                        "url": t.url,
                        "webSocketDebuggerUrl":
                            format!("ws://127.0.0.1:{port}/devtools/page/{}", t.target_id)
                    })
                }).collect()
            }).unwrap_or_default();
            serde_json::to_string(&targets).unwrap_or_default()
        }
        _ => json!([]).to_string(),
    };

    stream.write_all(http_json_response(&body).as_bytes()).await.ok();
}

async fn handle_websocket<S>(stream: S, state: Arc<Mutex<BrowserState>>)
where
    S: tokio::io::AsyncRead + tokio::io::AsyncWrite + Unpin + Send + 'static,
{
    let ws = match tokio_tungstenite::accept_async(stream).await {
        Ok(ws) => ws,
        Err(_) => return,
    };
    let (mut ws_tx, mut ws_rx) = ws.split();
    let (tx, mut rx) = mpsc::unbounded_channel::<String>();

    tokio::spawn(async move {
        while let Some(msg) = rx.recv().await {
            if ws_tx.send(Message::Text(msg.into())).await.is_err() { break; }
        }
    });

    while let Some(Ok(msg)) = ws_rx.next().await {
        let text = match msg {
            Message::Text(t) => t.to_string(),
            Message::Close(_) => break,
            _ => continue,
        };
        let cdp_msg: CdpMsg = match serde_json::from_str(&text) {
            Ok(m) => m,
            Err(_) => continue,
        };
        let responses = state.lock().map(|mut s| s.handle(&cdp_msg)).unwrap_or_default();
        for r in responses { tx.send(r.to_text()).ok(); }
    }
}

// ─── PeekedStream: prepend already-consumed header bytes back to the stream ───
// Both Cursor<Vec<u8>> and TcpStream are Unpin, so no pin-projection macro needed.

struct PeekedStream {
    peeked: std::io::Cursor<Vec<u8>>,
    inner: TcpStream,
}

impl Unpin for PeekedStream {}

impl PeekedStream {
    fn new(inner: TcpStream, peeked: Vec<u8>) -> Self {
        Self { peeked: std::io::Cursor::new(peeked), inner }
    }
}

impl tokio::io::AsyncRead for PeekedStream {
    fn poll_read(
        self: std::pin::Pin<&mut Self>,
        cx: &mut std::task::Context<'_>,
        buf: &mut tokio::io::ReadBuf<'_>,
    ) -> std::task::Poll<std::io::Result<()>> {
        let this = self.get_mut();
        let pos = this.peeked.position() as usize;
        if pos < this.peeked.get_ref().len() {
            let data = &this.peeked.get_ref()[pos..];
            let n = data.len().min(buf.remaining());
            buf.put_slice(&data[..n]);
            this.peeked.set_position((pos + n) as u64);
            return std::task::Poll::Ready(Ok(()));
        }
        std::pin::Pin::new(&mut this.inner).poll_read(cx, buf)
    }
}

impl tokio::io::AsyncWrite for PeekedStream {
    fn poll_write(
        self: std::pin::Pin<&mut Self>,
        cx: &mut std::task::Context<'_>,
        buf: &[u8],
    ) -> std::task::Poll<std::io::Result<usize>> {
        std::pin::Pin::new(&mut self.get_mut().inner).poll_write(cx, buf)
    }
    fn poll_flush(
        self: std::pin::Pin<&mut Self>,
        cx: &mut std::task::Context<'_>,
    ) -> std::task::Poll<std::io::Result<()>> {
        std::pin::Pin::new(&mut self.get_mut().inner).poll_flush(cx)
    }
    fn poll_shutdown(
        self: std::pin::Pin<&mut Self>,
        cx: &mut std::task::Context<'_>,
    ) -> std::task::Poll<std::io::Result<()>> {
        std::pin::Pin::new(&mut self.get_mut().inner).poll_shutdown(cx)
    }
}

// ─── Public CdpServer ────────────────────────────────────────────────────────

pub struct CdpServer {
    state: Arc<Mutex<BrowserState>>,
    listener: TcpListener,
    pub port: u16,
}

impl CdpServer {
    pub async fn bind(addr: &str) -> Result<Self, String> {
        let listener = TcpListener::bind(addr).await.map_err(|e| e.to_string())?;
        let port = listener.local_addr().map(|a| a.port()).unwrap_or(9222);
        let mut state = BrowserState::new(port);
        state.create_target("about:blank");
        Ok(Self { state: Arc::new(Mutex::new(state)), listener, port })
    }

    pub fn create_target(&self, url: &str) -> String {
        self.state.lock().map(|mut s| s.create_target(url)).unwrap_or_default()
    }

    pub fn attach(&self, target_id: &str) -> Option<String> {
        self.state.lock().ok().and_then(|mut s| s.attach_session(target_id))
    }

    pub fn detach(&self, session_id: &str) -> bool {
        self.state.lock().map(|mut s| s.detach_session(session_id)).unwrap_or(false)
    }

    pub fn targets(&self) -> Vec<TargetInfo> {
        self.state.lock().map(|s| s.target_info_list()).unwrap_or_default()
    }

    pub fn evaluate_in_session(
        &self,
        session_id: &str,
        expression: &str,
    ) -> Option<Result<JsValue, JsError>> {
        let mut guard = self.state.lock().ok()?;
        let target_id = guard.sessions.get(session_id)?.clone();
        let target = guard.targets.get_mut(&target_id)?;
        Some(target.context.eval(expression))
    }

    pub fn evaluate_in_target(
        &self,
        target_id: &str,
        expression: &str,
    ) -> Option<Result<JsValue, JsError>> {
        let mut guard = self.state.lock().ok()?;
        let target = guard.targets.get_mut(target_id)?;
        Some(target.context.eval(expression))
    }

    pub fn debugger_url(&self) -> String {
        format!("ws://127.0.0.1:{}", self.port)
    }

    pub async fn run(self) {
        let state = self.state;
        loop {
            match self.listener.accept().await {
                Ok((stream, _)) => {
                    let s = state.clone();
                    tokio::spawn(async move { handle_tcp(stream, s).await });
                }
                Err(_) => break,
            }
        }
    }
}
