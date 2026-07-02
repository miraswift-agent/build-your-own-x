use std::sync::{Arc, Mutex};

use rquickjs::prelude::Rest;

use crate::js::context::{
    ConsoleMessage, ContextId, JsError, JsProperty, JsValue, LogLevel, WorldType,
};

// ─── Engine trait (swap QuickJS → V8 by implementing this) ───────────────────

pub trait JsEngine: Send + Sync {
    fn create_context(&self) -> Result<Box<dyn JsContext + Send>, JsError>;
    fn create_utility_context(&self) -> Result<Box<dyn JsContext + Send>, JsError>;
}

pub trait JsContext: Send {
    fn eval(&mut self, script: &str) -> Result<JsValue, JsError>;
    fn eval_as_string(&mut self, script: &str) -> Result<String, JsError> {
        Ok(self.eval(script)?.to_string())
    }
    fn get_properties(&mut self, object_id: &str) -> Result<Vec<JsProperty>, JsError>;
    fn take_console_logs(&mut self) -> Vec<ConsoleMessage>;
    fn context_id(&self) -> ContextId;
    fn world(&self) -> WorldType;
}

// ─── QuickJS engine ──────────────────────────────────────────────────────────

pub struct QuickJsEngine;

impl QuickJsEngine {
    pub fn new() -> Self { Self }
}

impl Default for QuickJsEngine {
    fn default() -> Self { Self::new() }
}

impl JsEngine for QuickJsEngine {
    fn create_context(&self) -> Result<Box<dyn JsContext + Send>, JsError> {
        Ok(Box::new(QuickJsContext::new(ContextId::next(), WorldType::Main)?))
    }

    fn create_utility_context(&self) -> Result<Box<dyn JsContext + Send>, JsError> {
        Ok(Box::new(QuickJsContext::new(ContextId::next(), WorldType::Utility)?))
    }
}

// ─── QuickJS context ─────────────────────────────────────────────────────────

pub struct QuickJsContext {
    // Runtime must be kept alive for the lifetime of Context.
    _runtime: rquickjs::Runtime,
    context: rquickjs::Context,
    console_logs: Arc<Mutex<Vec<ConsoleMessage>>>,
    id: ContextId,
    world: WorldType,
}

impl QuickJsContext {
    pub fn new(id: ContextId, world: WorldType) -> Result<Self, JsError> {
        let runtime = rquickjs::Runtime::new()
            .map_err(|e| JsError::new(format!("QuickJS runtime: {e}")))?;
        let context = rquickjs::Context::full(&runtime)
            .map_err(|e| JsError::new(format!("QuickJS context: {e}")))?;
        let logs: Arc<Mutex<Vec<ConsoleMessage>>> = Arc::new(Mutex::new(Vec::new()));

        setup_console(&context, &logs)
            .map_err(|e| JsError::new(format!("console setup: {e}")))?;
        setup_timers(&context)
            .map_err(|e| JsError::new(format!("timer setup: {e}")))?;

        Ok(Self { _runtime: runtime, context, console_logs: logs, id, world })
    }
}

// ─── Console API setup ───────────────────────────────────────────────────────

fn setup_console(
    ctx: &rquickjs::Context,
    logs: &Arc<Mutex<Vec<ConsoleMessage>>>,
) -> Result<(), String> {
    let logs = logs.clone();
    ctx.with(|ctx| -> rquickjs::Result<()> {
        let globals = ctx.globals();
        let console = rquickjs::Object::new(ctx.clone())?;

        for (name, level) in &[
            ("log", LogLevel::Log),
            ("error", LogLevel::Error),
            ("warn", LogLevel::Warn),
            ("info", LogLevel::Info),
            ("debug", LogLevel::Debug),
        ] {
            let logs_clone = logs.clone();
            let level = level.clone();
            let func = rquickjs::Function::new(
                ctx.clone(),
                move |args: Rest<rquickjs::Value>| -> rquickjs::Result<()> {
                    let message = args
                        .iter()
                        .map(rqs_to_display)
                        .collect::<Vec<_>>()
                        .join(" ");
                    if let Ok(mut g) = logs_clone.lock() {
                        g.push(ConsoleMessage {
                            level: level.clone(),
                            message,
                            url: None,
                            line: None,
                            column: None,
                        });
                    }
                    Ok(())
                },
            )?;
            console.set(*name, func)?;
        }

        globals.set("console", console)?;
        Ok(())
    })
    .map_err(|e| e.to_string())
}

// ─── Timer stubs ─────────────────────────────────────────────────────────────

fn setup_timers(ctx: &rquickjs::Context) -> Result<(), String> {
    ctx.with(|ctx| -> rquickjs::Result<()> {
        let globals = ctx.globals();

        let st = rquickjs::Function::new(
            ctx.clone(),
            |_cb: rquickjs::Value, _delay: rquickjs::Value| -> rquickjs::Result<i32> { Ok(1) },
        )?;
        globals.set("setTimeout", st)?;

        let si = rquickjs::Function::new(
            ctx.clone(),
            |_cb: rquickjs::Value, _delay: rquickjs::Value| -> rquickjs::Result<i32> { Ok(1) },
        )?;
        globals.set("setInterval", si)?;

        let ct = rquickjs::Function::new(
            ctx.clone(),
            |_id: rquickjs::Value| -> rquickjs::Result<()> { Ok(()) },
        )?;
        globals.set("clearTimeout", ct.clone())?;
        globals.set("clearInterval", ct)?;

        Ok(())
    })
    .map_err(|e| e.to_string())
}

// ─── Value conversion helpers ────────────────────────────────────────────────

fn rqs_to_display(val: &rquickjs::Value) -> String {
    use rquickjs::Type;
    match val.type_of() {
        Type::Undefined => "undefined".into(),
        Type::Null => "null".into(),
        Type::Bool => val.as_bool().map_or("false".into(), |b| b.to_string()),
        Type::Int => val.as_int().map_or("0".into(), |n| n.to_string()),
        Type::Float => val.as_float().map_or("0".into(), |n| {
            if n.fract() == 0.0 { format!("{}", n as i64) } else { n.to_string() }
        }),
        Type::String => val
            .as_string()
            .and_then(|s| s.to_string().ok())
            .unwrap_or_default(),
        Type::Object | Type::Exception => {
            // Try to extract .message from Error objects; fall back to generic display
            if let Some(obj) = val.as_object() {
                if let Ok(msg) = obj.get::<_, String>("message") {
                    if !msg.is_empty() {
                        return msg;
                    }
                }
            }
            "[object Object]".into()
        }
        Type::Array => {
            if let Some(arr) = val.as_array() {
                let items: Vec<String> = (0..arr.len())
                    .filter_map(|i| arr.get::<rquickjs::Value>(i).ok())
                    .map(|v| rqs_to_display(&v))
                    .collect();
                format!("[{}]", items.join(", "))
            } else {
                "[]".into()
            }
        }
        Type::Function | Type::Constructor => "function".into(),
        _ => "[object]".into(),
    }
}

pub(crate) fn rqs_to_jsvalue(val: &rquickjs::Value) -> JsValue {
    use rquickjs::Type;
    use std::collections::HashMap;

    match val.type_of() {
        Type::Undefined => JsValue::Undefined,
        Type::Null => JsValue::Null,
        Type::Bool => JsValue::Bool(val.as_bool().unwrap_or(false)),
        Type::Int => JsValue::Number(val.as_int().unwrap_or(0) as f64),
        Type::Float => JsValue::Number(val.as_float().unwrap_or(0.0)),
        Type::String => JsValue::String(
            val.as_string().and_then(|s| s.to_string().ok()).unwrap_or_default(),
        ),
        Type::Array => {
            if let Some(arr) = val.as_array() {
                let items = (0..arr.len())
                    .filter_map(|i| arr.get::<rquickjs::Value>(i).ok())
                    .map(|v| rqs_to_jsvalue(&v))
                    .collect();
                JsValue::Array(items)
            } else {
                JsValue::Null
            }
        }
        Type::Object | Type::Exception => JsValue::Object(HashMap::new()),
        Type::Function | Type::Constructor => JsValue::Function,
        _ => JsValue::Undefined,
    }
}

fn extract_exception(val: rquickjs::Value) -> JsError {
    // In rquickjs 0.12, caught exceptions have Type::Exception (a subtype of Object).
    // as_object() won't match the type tag for Exception values,
    // but as_exception() checks is_error() which works for Error objects.
    if let Some(exc) = val.as_exception() {
        let message = exc.message().unwrap_or_else(|| "Unknown error".into());
        let stack = exc.stack();
        let (line, column) = parse_location_from_stack(stack.as_deref());
        return JsError { message, stack, line, column, source_url: None };
    }

    // Fallback: regular object throws
    if let Some(obj) = val.as_object() {
        let message = obj
            .get::<_, String>("message")
            .ok()
            .filter(|m| !m.is_empty())
            .or_else(|| obj.get::<_, String>("description").ok())
            .unwrap_or_else(|| "Unknown error".into());
        let stack = obj.get::<_, String>("stack").ok();
        let (line, column) = parse_location_from_stack(stack.as_deref());
        return JsError { message, stack, line, column, source_url: None };
    }

    if let Some(s) = val.as_string() {
        return JsError::new(s.to_string().unwrap_or_default());
    }

    JsError::new(rqs_to_display(&val))
}

/// Parses `(line, column)` from a QuickJS-style stack trace.
/// Stack line format: "    at script:2:5" or "    at <eval>:2:5"
fn parse_location_from_stack(stack: Option<&str>) -> (Option<u32>, Option<u32>) {
    let stack = match stack { Some(s) => s, None => return (None, None) };

    for row in stack.lines().skip(1) {
        let trimmed = row.trim();
        if trimmed.starts_with("at ") {
            let parts: Vec<&str> = trimmed.rsplitn(3, ':').collect();
            if parts.len() >= 2 {
                let col = parts[0].trim().parse::<u32>().ok();
                let line = parts[1].trim().parse::<u32>().ok();
                if line.is_some() {
                    return (line, col);
                }
            }
        }
    }
    (None, None)
}

// ─── JsContext impl ───────────────────────────────────────────────────────────

impl JsContext for QuickJsContext {
    fn eval(&mut self, script: &str) -> Result<JsValue, JsError> {
        let script = script.to_string();
        self.context.with(|ctx| -> Result<JsValue, JsError> {
            match ctx.eval::<rquickjs::Value, _>(script.as_str()) {
                Ok(val) => Ok(rqs_to_jsvalue(&val)),
                Err(rquickjs::Error::Exception) => {
                    let exc = ctx.catch();
                    Err(extract_exception(exc))
                }
                Err(e) => Err(JsError::new(e.to_string())),
            }
        })
    }

    fn get_properties(&mut self, _object_id: &str) -> Result<Vec<JsProperty>, JsError> {
        Ok(vec![])
    }

    fn take_console_logs(&mut self) -> Vec<ConsoleMessage> {
        self.console_logs.lock().map(|mut g| std::mem::take(&mut *g)).unwrap_or_default()
    }

    fn context_id(&self) -> ContextId { self.id }
    fn world(&self) -> WorldType { self.world.clone() }
}