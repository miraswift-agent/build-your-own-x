use std::collections::HashMap;
use std::time::{Duration, SystemTime};

#[derive(Debug, Clone, PartialEq)]
pub enum SameSite {
    Strict,
    Lax,
    None,
}

#[derive(Debug, Clone)]
pub struct Cookie {
    pub name: String,
    pub value: String,
    pub domain: Option<String>,
    pub path: Option<String>,
    pub expires: Option<SystemTime>,
    pub secure: bool,
    pub http_only: bool,
    pub same_site: SameSite,
    pub host_only: bool,
}

impl Cookie {
    pub fn is_expired(&self) -> bool {
        if let Some(expires) = self.expires {
            return SystemTime::now() > expires;
        }
        false
    }

    pub fn is_session(&self) -> bool {
        self.expires.is_none()
    }
}

pub fn parse_set_cookie(header: &str, request_domain: &str, request_path: &str) -> Option<Cookie> {
    let semicolon = header.find(';');
    let name_value_str = match semicolon {
        Some(i) => &header[..i],
        None => header,
    };
    let attrs_str = match semicolon {
        Some(i) => &header[i + 1..],
        None => "",
    };

    let eq = name_value_str.find('=')?;
    let name = name_value_str[..eq].trim().to_string();
    if name.is_empty() {
        return None;
    }
    let value = name_value_str[eq + 1..].trim().to_string();

    let mut cookie = Cookie {
        name,
        value,
        domain: None,
        path: None,
        expires: None,
        secure: false,
        http_only: false,
        same_site: SameSite::Lax,
        host_only: true,
    };

    for attr in attrs_str.split(';') {
        let attr = attr.trim();
        if attr.is_empty() {
            continue;
        }
        let (key, val) = if let Some(pos) = attr.find('=') {
            (attr[..pos].trim(), Some(attr[pos + 1..].trim()))
        } else {
            (attr, None)
        };

        match key.to_lowercase().as_str() {
            "domain" => {
                if let Some(v) = val {
                    let d = v.trim_start_matches('.').to_lowercase();
                    if !d.is_empty() {
                        cookie.domain = Some(d);
                        cookie.host_only = false;
                    }
                }
            }
            "path" => {
                cookie.path = val.map(|v| v.to_string());
            }
            "expires" => {
                if let Some(v) = val {
                    cookie.expires = parse_http_date(v);
                }
            }
            "max-age" => {
                if let Some(v) = val {
                    if let Ok(n) = v.trim().parse::<i64>() {
                        if n <= 0 {
                            cookie.expires = Some(SystemTime::UNIX_EPOCH);
                        } else {
                            cookie.expires =
                                Some(SystemTime::now() + Duration::from_secs(n as u64));
                        }
                    }
                }
            }
            "secure" => cookie.secure = true,
            "httponly" => cookie.http_only = true,
            "samesite" => {
                cookie.same_site = match val.map(|v| v.to_lowercase()).as_deref() {
                    Some("strict") => SameSite::Strict,
                    Some("none") => SameSite::None,
                    _ => SameSite::Lax,
                };
            }
            _ => {}
        }
    }

    if cookie.domain.is_none() {
        cookie.domain = Some(request_domain.to_lowercase());
        cookie.host_only = true;
    }
    if cookie.path.is_none() {
        cookie.path = Some(default_path(request_path));
    }

    Some(cookie)
}

fn default_path(request_path: &str) -> String {
    if !request_path.starts_with('/') {
        return "/".to_string();
    }
    match request_path.rfind('/') {
        Some(0) | None => "/".to_string(),
        Some(i) => request_path[..i].to_string(),
    }
}

fn parse_http_date(s: &str) -> Option<SystemTime> {
    let months = [
        "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec",
    ];
    let s_lower = s.to_lowercase();
    let parts: Vec<&str> = s_lower.split_whitespace().collect();

    // Expect: "Weekday, DD Mon YYYY HH:MM:SS GMT"
    if parts.len() < 5 {
        return None;
    }
    let (d_str, mon_str, y_str, time_str) = if parts[0].ends_with(',') {
        if parts.len() < 5 {
            return None;
        }
        (parts[1], parts[2], parts[3], parts[4])
    } else {
        return None;
    };

    let day: u32 = d_str.parse().ok()?;
    let month: u32 = months.iter().position(|&m| m == mon_str)? as u32 + 1;
    let year: u32 = y_str.parse().ok()?;

    let time_parts: Vec<&str> = time_str.split(':').collect();
    if time_parts.len() != 3 {
        return None;
    }
    let hours: u64 = time_parts[0].parse().ok()?;
    let minutes: u64 = time_parts[1].parse().ok()?;
    let seconds: u64 = time_parts[2].parse().ok()?;

    let days = days_from_epoch(year, month, day)?;
    let secs = days as u64 * 86400 + hours * 3600 + minutes * 60 + seconds;
    Some(SystemTime::UNIX_EPOCH + Duration::from_secs(secs))
}

fn days_from_epoch(year: u32, month: u32, day: u32) -> Option<u32> {
    if year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 {
        return None;
    }
    let mut days = 0u32;
    for y in 1970..year {
        days += if is_leap(y) { 366 } else { 365 };
    }
    let month_days = [
        31u32,
        28 + if is_leap(year) { 1 } else { 0 },
        31,
        30,
        31,
        30,
        31,
        31,
        30,
        31,
        30,
        31,
    ];
    for m in 0..(month - 1) as usize {
        days += month_days[m];
    }
    days += day - 1;
    Some(days)
}

fn is_leap(y: u32) -> bool {
    (y % 4 == 0 && y % 100 != 0) || y % 400 == 0
}

#[derive(Debug, Default)]
pub struct CookieJar {
    cookies: HashMap<String, Vec<Cookie>>,
}

impl CookieJar {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn store(&mut self, cookie: Cookie) {
        let domain = cookie.domain.clone().unwrap_or_default();
        if cookie.is_expired() {
            if let Some(list) = self.cookies.get_mut(&domain) {
                let name = cookie.name.clone();
                let path = cookie.path.clone();
                list.retain(|c| !(c.name == name && c.path == path));
            }
            return;
        }
        let list = self.cookies.entry(domain).or_default();
        let name = cookie.name.clone();
        let path = cookie.path.clone();
        list.retain(|c| !(c.name == name && c.path == path));
        list.push(cookie);
    }

    pub fn cookies_for(&self, domain: &str, path: &str, secure: bool) -> Vec<&Cookie> {
        let domain_lower = domain.to_lowercase();
        let mut result: Vec<&Cookie> = self
            .cookies
            .iter()
            .flat_map(|(cookie_domain, cookies)| {
                let domain_lower = domain_lower.clone();
                cookies.iter().filter(move |c| {
                    !c.is_expired()
                        && (!c.secure || secure)
                        && domain_matches(cookie_domain, &domain_lower, c.host_only)
                        && path_matches(c.path.as_deref().unwrap_or("/"), path)
                })
            })
            .collect();

        result.sort_by(|a, b| {
            let pa = a.path.as_deref().unwrap_or("/");
            let pb = b.path.as_deref().unwrap_or("/");
            pb.len().cmp(&pa.len())
        });
        result
    }

    pub fn cookie_header(&self, domain: &str, path: &str, secure: bool) -> String {
        self.cookies_for(domain, path, secure)
            .iter()
            .map(|c| format!("{}={}", c.name, c.value))
            .collect::<Vec<_>>()
            .join("; ")
    }

    pub fn len(&self) -> usize {
        self.cookies.values().map(|v| v.len()).sum()
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

fn domain_matches(cookie_domain: &str, request_domain: &str, host_only: bool) -> bool {
    if host_only {
        cookie_domain == request_domain
    } else {
        request_domain == cookie_domain
            || request_domain.ends_with(&format!(".{cookie_domain}"))
    }
}

fn path_matches(cookie_path: &str, request_path: &str) -> bool {
    if request_path == cookie_path {
        return true;
    }
    if request_path.starts_with(cookie_path) {
        return cookie_path.ends_with('/') || request_path[cookie_path.len()..].starts_with('/');
    }
    false
}
