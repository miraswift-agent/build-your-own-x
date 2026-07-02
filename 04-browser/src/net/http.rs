use std::collections::HashMap;
use std::time::Duration;

use http::{Method, Request};
use http_body_util::{BodyExt, Full};
use hyper::body::Bytes;
use hyper_util::client::legacy::Client;
use hyper_util::rt::TokioExecutor;

use crate::net::cookies::{parse_set_cookie, CookieJar};
use crate::net::tls::verified_connector;
use crate::net::url::Url;

type HyperClient =
    Client<hyper_rustls::HttpsConnector<hyper_util::client::legacy::connect::HttpConnector>, Full<Bytes>>;

#[derive(Debug, Clone)]
pub struct HttpConfig {
    pub max_redirects: usize,
    pub connect_timeout: Duration,
    pub read_timeout: Duration,
    pub user_agent: String,
    pub verify_certs: bool,
}

impl Default for HttpConfig {
    fn default() -> Self {
        Self {
            max_redirects: 10,
            connect_timeout: Duration::from_secs(10),
            read_timeout: Duration::from_secs(30),
            user_agent: "agent-browser/0.3 (build-your-own-x)".to_string(),
            verify_certs: true,
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

        loop {
            let mut response =
                self.single_request(&current_method, &current_url, body.as_deref(), jar)
                    .await?;

            // Store cookies from response
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
            let is_redirect = matches!(status, 301 | 302 | 303 | 307 | 308);

            if is_redirect {
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
                    current_url = new_url;
                    // 303 always becomes GET; 301/302 typically become GET for POST
                    if matches!(status, 301 | 302 | 303) {
                        current_method = Method::GET;
                    }
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

        let body_bytes = tokio::time::timeout(self.config.read_timeout, res.into_body().collect())
            .await
            .map_err(|_| "Body read timed out".to_string())?
            .map_err(|e| format!("Body read error: {e}"))?
            .to_bytes()
            .to_vec();

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
            final_url: url.clone(), // updated by caller
            content_type,
            redirect_count: 0,
        })
    }
}

pub fn detect_content_type(ct: &str) -> ContentType {
    let ct = ct.to_lowercase();
    let ct = ct.split(';').next().unwrap_or("").trim();
    match ct {
        "text/html" | "application/xhtml+xml" => ContentType::Html,
        "application/json" | "application/ld+json" => ContentType::Json,
        "text/css" => ContentType::Css,
        "application/javascript"
        | "text/javascript"
        | "application/x-javascript" => ContentType::JavaScript,
        t if t.starts_with("text/") => ContentType::Text,
        _ => ContentType::Binary,
    }
}
