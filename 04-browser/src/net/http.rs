use std::collections::HashMap;
use std::time::Duration;

use http::{Method, Request};
use http_body_util::{BodyExt, Full};
use hyper::body::{Body, Bytes};
use hyper_util::client::legacy::Client;
use hyper_util::rt::TokioExecutor;

use crate::net::cookies::{parse_set_cookie, CookieJar};
use crate::net::tls::verified_connector;
use crate::net::url::Url;

type HyperClient = Client<
    hyper_rustls::HttpsConnector<hyper_util::client::legacy::connect::HttpConnector>,
    Full<Bytes>,
>;

#[derive(Debug, Clone)]
pub struct HttpConfig {
    pub max_redirects: usize,
    pub connect_timeout: Duration,
    pub read_timeout: Duration,
    pub max_response_body_bytes: usize,
    pub user_agent: String,
}

impl Default for HttpConfig {
    fn default() -> Self {
        Self {
            max_redirects: 10,
            connect_timeout: Duration::from_secs(10),
            read_timeout: Duration::from_secs(30),
            max_response_body_bytes: 10 * 1024 * 1024,
            user_agent: "agent-browser/0.6 (partial-browser-semantics)".to_string(),
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum ContentType {
    Html,
    Json,
    Css,
    JavaScript,
    Text,
    Binary,
}

impl ContentType {
    pub fn as_str(&self) -> &'static str {
        match self {
            ContentType::Html => "text/html",
            ContentType::Json => "application/json",
            ContentType::Css => "text/css",
            ContentType::JavaScript => "application/javascript",
            ContentType::Text => "text/plain",
            ContentType::Binary => "application/octet-stream",
        }
    }
}

#[derive(Debug)]
pub struct HttpResponse {
    pub status: u16,
    pub headers: HashMap<String, Vec<String>>,
    pub body: Vec<u8>,
    pub final_url: Url,
    pub content_type: ContentType,
    pub redirect_count: usize,
}

impl HttpResponse {
    pub fn body_as_str(&self) -> &str {
        std::str::from_utf8(&self.body).unwrap_or("")
    }

    pub fn header(&self, name: &str) -> Option<&str> {
        self.headers
            .get(&name.to_lowercase())
            .and_then(|v| v.first())
            .map(|s| s.as_str())
    }
}

pub struct HttpClient {
    client: HyperClient,
    config: HttpConfig,
}

impl HttpClient {
    pub fn new(config: HttpConfig) -> Self {
        let connector = verified_connector();
        let client = Client::builder(TokioExecutor::new()).build(connector);
        Self { client, config }
    }

    pub fn with_defaults() -> Self {
        Self::new(HttpConfig::default())
    }

    pub async fn get(&self, url: &Url, jar: &mut CookieJar) -> Result<HttpResponse, String> {
        self.fetch(Method::GET, url, None, jar).await
    }

    pub async fn post(
        &self,
        url: &Url,
        body: Vec<u8>,
        jar: &mut CookieJar,
    ) -> Result<HttpResponse, String> {
        self.fetch(Method::POST, url, Some(body), jar).await
    }

    pub async fn head(&self, url: &Url, jar: &mut CookieJar) -> Result<HttpResponse, String> {
        self.fetch(Method::HEAD, url, None, jar).await
    }

    pub async fn put(
        &self,
        url: &Url,
        body: Vec<u8>,
        jar: &mut CookieJar,
    ) -> Result<HttpResponse, String> {
        self.fetch(Method::PUT, url, Some(body), jar).await
    }

    pub async fn delete(&self, url: &Url, jar: &mut CookieJar) -> Result<HttpResponse, String> {
        self.fetch(Method::DELETE, url, None, jar).await
    }

    async fn fetch(
        &self,
        method: Method,
        url: &Url,
        body: Option<Vec<u8>>,
        jar: &mut CookieJar,
    ) -> Result<HttpResponse, String> {
        let mut current_url = url.clone();
        let mut redirect_count = 0;
        let mut current_method = method.clone();
        let mut current_body = body.clone();

        loop {
            let mut response = self
                .single_request(&current_method, &current_url, current_body.as_deref(), jar)
                .await?;

            let domain = current_url.host().unwrap_or("").to_string();
            let path = current_url.path().to_string();
            if let Some(set_cookies) = response.headers.get("set-cookie").cloned() {
                for cookie_str in &set_cookies {
                    if let Some(cookie) = parse_set_cookie(cookie_str, &domain, &path) {
                        jar.store(cookie);
                    }
                }
            }

            let status = response.status;
            if matches!(status, 301 | 302 | 303 | 307 | 308) {
                if redirect_count >= self.config.max_redirects {
                    return Err(format!(
                        "Too many redirects (max {})",
                        self.config.max_redirects
                    ));
                }
                redirect_count += 1;

                let location = response
                    .headers
                    .get("location")
                    .and_then(|v| v.first())
                    .cloned();

                if let Some(loc) = location {
                    let new_url = Url::resolve(&current_url, &loc)
                        .map_err(|e| format!("Invalid redirect URL '{loc}': {e}"))?;
                    let decision =
                        redirect_behavior(status, &current_method, current_body.is_some());
                    current_url = new_url;
                    current_method = decision.method;
                    current_body = if decision.preserve_body {
                        current_body
                    } else {
                        None
                    };
                    continue;
                }
            }

            response.final_url = current_url;
            response.redirect_count = redirect_count;
            return Ok(response);
        }
    }

    async fn single_request(
        &self,
        method: &Method,
        url: &Url,
        body: Option<&[u8]>,
        jar: &CookieJar,
    ) -> Result<HttpResponse, String> {
        let uri: http::Uri = url
            .as_str()
            .parse()
            .map_err(|e| format!("Invalid URI '{}': {e}", url.as_str()))?;

        let secure = url.scheme() == "https";
        let domain = url.host().unwrap_or("");
        let path = url.path();
        let cookie_header = jar.cookie_header(domain, path, secure);

        let body_bytes = Bytes::copy_from_slice(body.unwrap_or(&[]));

        let mut req_builder = Request::builder()
            .method(method.clone())
            .uri(uri)
            .header("user-agent", &self.config.user_agent)
            .header(
                "accept",
                "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
            )
            .header("accept-language", "en-US,en;q=0.5")
            .header("connection", "keep-alive");

        if !cookie_header.is_empty() {
            req_builder = req_builder.header("cookie", cookie_header);
        }

        let req = req_builder
            .body(Full::new(body_bytes))
            .map_err(|e| format!("Build request: {e}"))?;

        let res = tokio::time::timeout(self.config.read_timeout, self.client.request(req))
            .await
            .map_err(|_| {
                format!(
                    "Request timed out after {}s",
                    self.config.read_timeout.as_secs()
                )
            })?
            .map_err(|e| format!("HTTP error: {e}"))?;

        let status = res.status().as_u16();

        let mut headers: HashMap<String, Vec<String>> = HashMap::new();
        for (key, value) in res.headers() {
            let k = key.as_str().to_lowercase();
            let v = value.to_str().unwrap_or("").to_string();
            headers.entry(k).or_default().push(v);
        }

        let body_bytes = collect_body_limited(
            res.into_body(),
            self.config.max_response_body_bytes,
            self.config.read_timeout,
        )
        .await?;

        let ct_str = headers
            .get("content-type")
            .and_then(|v| v.first())
            .map(|s| s.as_str())
            .unwrap_or("");
        let content_type = detect_content_type(ct_str);

        Ok(HttpResponse {
            status,
            headers,
            body: body_bytes,
            final_url: url.clone(),
            content_type,
            redirect_count: 0,
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct RedirectDecision {
    method: Method,
    preserve_body: bool,
}

fn redirect_behavior(status: u16, method: &Method, has_body: bool) -> RedirectDecision {
    match status {
        303 => {
            if method == Method::HEAD {
                RedirectDecision {
                    method: Method::HEAD,
                    preserve_body: false,
                }
            } else {
                RedirectDecision {
                    method: Method::GET,
                    preserve_body: false,
                }
            }
        }
        301 | 302 if method == Method::POST => RedirectDecision {
            method: Method::GET,
            preserve_body: false,
        },
        301 | 302 | 307 | 308 => RedirectDecision {
            method: method.clone(),
            preserve_body: has_body,
        },
        _ => RedirectDecision {
            method: method.clone(),
            preserve_body: has_body,
        },
    }
}

async fn collect_body_limited<B>(
    mut body: B,
    max_bytes: usize,
    timeout: Duration,
) -> Result<Vec<u8>, String>
where
    B: Body<Data = Bytes> + Unpin,
    B::Error: std::fmt::Display,
{
    let mut out = Vec::new();
    while let Some(frame) = tokio::time::timeout(timeout, body.frame())
        .await
        .map_err(|_| "Body read timed out".to_string())?
    {
        let frame = frame.map_err(|e| format!("Body read error: {e}"))?;
        if let Some(data) = frame.data_ref() {
            if out.len().saturating_add(data.len()) > max_bytes {
                return Err(format!(
                    "Response body exceeded limit ({} bytes)",
                    max_bytes
                ));
            }
            out.extend_from_slice(data);
        }
    }
    Ok(out)
}

pub fn detect_content_type(ct: &str) -> ContentType {
    let ct = ct.to_lowercase();
    let ct = ct.split(';').next().unwrap_or("").trim();
    match ct {
        "text/html" | "application/xhtml+xml" => ContentType::Html,
        "application/json" | "application/ld+json" => ContentType::Json,
        "text/css" => ContentType::Css,
        "application/javascript" | "text/javascript" | "application/x-javascript" => {
            ContentType::JavaScript
        }
        t if t.starts_with("text/") => ContentType::Text,
        _ => ContentType::Binary,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn redirect_policy_rewrites_post_for_301_302_303() {
        let d301 = redirect_behavior(301, &Method::POST, true);
        let d302 = redirect_behavior(302, &Method::POST, true);
        let d303 = redirect_behavior(303, &Method::POST, true);

        assert_eq!(d301.method, Method::GET);
        assert!(!d301.preserve_body);
        assert_eq!(d302.method, Method::GET);
        assert!(!d302.preserve_body);
        assert_eq!(d303.method, Method::GET);
        assert!(!d303.preserve_body);
    }

    #[test]
    fn redirect_policy_preserves_non_post_for_301_302_and_head_for_303() {
        let head_303 = redirect_behavior(303, &Method::HEAD, false);
        let put_301 = redirect_behavior(301, &Method::PUT, true);
        let delete_302 = redirect_behavior(302, &Method::DELETE, false);

        assert_eq!(head_303.method, Method::HEAD);
        assert!(!head_303.preserve_body);
        assert_eq!(put_301.method, Method::PUT);
        assert!(put_301.preserve_body);
        assert_eq!(delete_302.method, Method::DELETE);
        assert!(!delete_302.preserve_body);
    }

    #[test]
    fn redirect_policy_preserves_method_and_body_for_307_308() {
        let d307 = redirect_behavior(307, &Method::POST, true);
        let d308 = redirect_behavior(308, &Method::PUT, true);

        assert_eq!(d307.method, Method::POST);
        assert!(d307.preserve_body);
        assert_eq!(d308.method, Method::PUT);
        assert!(d308.preserve_body);
    }

    #[tokio::test]
    async fn collect_body_limited_accepts_body_under_cap() {
        let body = Full::new(Bytes::from_static(b"hello world"));
        let bytes = collect_body_limited(body, 32, Duration::from_secs(1))
            .await
            .unwrap();
        assert_eq!(bytes, b"hello world");
    }

    #[tokio::test]
    async fn collect_body_limited_rejects_body_over_cap() {
        let body = Full::new(Bytes::from(vec![b'x'; 64]));
        let err = collect_body_limited(body, 16, Duration::from_secs(1))
            .await
            .unwrap_err();
        assert!(err.contains("exceeded limit"));
    }
}
