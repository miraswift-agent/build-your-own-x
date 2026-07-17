use std::collections::HashMap;

/// A JavaScript value, engine-agnostic representation.
#[derive(Debug, Clone, PartialEq)]
pub enum JsValue {
    Undefined,
    Null,
    Bool(bool),
    Number(f64),
    String(String),
    Object(HashMap<String, JsValue>),
    Array(Vec<JsValue>),
    Function,
}

impl JsValue {
    pub fn as_bool(&self) -> Option<bool> {
        match self {
            JsValue::Bool(b) => Some(*b),
            _ => None,
        }
    }

    pub fn as_number(&self) -> Option<f64> {
        match self {
            JsValue::Number(n) => Some(*n),
            _ => None,
        }
    }

    pub fn as_str(&self) -> Option<&str> {
        match self {
            JsValue::String(s) => Some(s.as_str()),
            _ => None,
        }
    }

    pub fn is_undefined(&self) -> bool {
        matches!(self, JsValue::Undefined)
    }
    pub fn is_null(&self) -> bool {
        matches!(self, JsValue::Null)
    }

    /// CDP type string for Runtime.RemoteObject
    pub fn type_str(&self) -> &'static str {
        match self {
            JsValue::Undefined => "undefined",
            JsValue::Null => "object",
            JsValue::Bool(_) => "boolean",
            JsValue::Number(_) => "number",
            JsValue::String(_) => "string",
            JsValue::Object(_) => "object",
            JsValue::Array(_) => "object",
            JsValue::Function => "function",
        }
    }

    pub fn subtype_str(&self) -> Option<&'static str> {
        match self {
            JsValue::Null => Some("null"),
            JsValue::Array(_) => Some("array"),
            _ => None,
        }
    }

    pub fn to_json(&self) -> serde_json::Value {
        match self {
            JsValue::Undefined | JsValue::Function => serde_json::Value::Null,
            JsValue::Null => serde_json::Value::Null,
            JsValue::Bool(b) => serde_json::Value::Bool(*b),
            JsValue::Number(n) => {
                if n.fract() == 0.0 && n.abs() < 1e15 {
                    serde_json::Value::Number(serde_json::Number::from(*n as i64))
                } else {
                    serde_json::json!(n)
                }
            }
            JsValue::String(s) => serde_json::Value::String(s.clone()),
            JsValue::Array(arr) => {
                serde_json::Value::Array(arr.iter().map(|v| v.to_json()).collect())
            }
            JsValue::Object(obj) => {
                let map: serde_json::Map<String, serde_json::Value> =
                    obj.iter().map(|(k, v)| (k.clone(), v.to_json())).collect();
                serde_json::Value::Object(map)
            }
        }
    }
}

impl std::fmt::Display for JsValue {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            JsValue::Undefined => write!(f, "undefined"),
            JsValue::Null => write!(f, "null"),
            JsValue::Bool(b) => write!(f, "{b}"),
            JsValue::Number(n) => {
                if n.fract() == 0.0 && n.abs() < 1e15 {
                    write!(f, "{}", *n as i64)
                } else {
                    write!(f, "{n}")
                }
            }
            JsValue::String(s) => write!(f, "{s}"),
            JsValue::Object(_) => write!(f, "[object Object]"),
            JsValue::Array(arr) => {
                write!(
                    f,
                    "{}",
                    arr.iter()
                        .map(|v| v.to_string())
                        .collect::<Vec<_>>()
                        .join(",")
                )
            }
            JsValue::Function => write!(f, "function"),
        }
    }
}

/// A JavaScript error with optional stack trace and location.
#[derive(Debug, Clone)]
pub struct JsError {
    pub message: String,
    pub stack: Option<String>,
    pub line: Option<u32>,
    pub column: Option<u32>,
    pub source_url: Option<String>,
}

impl JsError {
    pub fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
            stack: None,
            line: None,
            column: None,
            source_url: None,
        }
    }
}

impl std::fmt::Display for JsError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.message)?;
        if let Some(line) = self.line {
            write!(f, " (line {line}")?;
            if let Some(col) = self.column {
                write!(f, ":{col}")?;
            }
            write!(f, ")")?;
        }
        Ok(())
    }
}

impl std::error::Error for JsError {}

/// Severity of a console API call.
#[derive(Debug, Clone, PartialEq)]
pub enum LogLevel {
    Log,
    Error,
    Warn,
    Info,
    Debug,
}

impl LogLevel {
    pub fn as_str(&self) -> &'static str {
        match self {
            LogLevel::Log => "log",
            LogLevel::Error => "error",
            LogLevel::Warn => "warn",
            LogLevel::Info => "info",
            LogLevel::Debug => "debug",
        }
    }
}

/// A captured console.* call.
#[derive(Debug, Clone)]
pub struct ConsoleMessage {
    pub level: LogLevel,
    pub message: String,
    pub url: Option<String>,
    pub line: Option<u32>,
    pub column: Option<u32>,
}

/// A property of a remote JS object.
#[derive(Debug, Clone)]
pub struct JsProperty {
    pub name: String,
    pub value: JsValue,
    pub writable: bool,
    pub enumerable: bool,
    pub configurable: bool,
}

/// Which isolation world a context lives in.
#[derive(Debug, Clone, PartialEq)]
pub enum WorldType {
    /// Page script context.
    Main,
    /// Agent-injected utilities; can access DOM but not page globals.
    Utility,
}

/// Monotonically-increasing ID for execution contexts (CDP ContextId).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct ContextId(pub u64);

impl ContextId {
    pub fn next() -> Self {
        use std::sync::atomic::{AtomicU64, Ordering};
        static COUNTER: AtomicU64 = AtomicU64::new(1);
        ContextId(COUNTER.fetch_add(1, Ordering::SeqCst))
    }

    pub fn as_u64(self) -> u64 {
        self.0
    }
}

impl std::fmt::Display for ContextId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}
