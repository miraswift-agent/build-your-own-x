/// Same-origin / CSP helper types.
///
/// Note: this module currently provides parsing and comparison primitives, not
/// full browser-grade runtime enforcement across every navigation/eval path.

#[derive(Debug, Clone, PartialEq)]
pub struct Origin {
    pub scheme: String,
    pub host: String,
    pub port: Option<u16>,
}

impl Origin {
    /// Parse an origin from a URL string.
    pub fn from_url(url: &str) -> Option<Self> {
        // Strip fragment and query for origin comparison
        let url = url.split('#').next().unwrap_or(url);
        let url = url.split('?').next().unwrap_or(url);

        let (scheme, rest) = url.split_once("://")?;
        let authority = rest.split('/').next().unwrap_or(rest);

        let (host_part, explicit_port) = if let Some((h, p)) = authority.rsplit_once(':') {
            let port: u16 = p.parse().ok()?;
            (h, Some(port))
        } else {
            (authority, None)
        };

        let default_port: Option<u16> = match scheme {
            "http" => Some(80),
            "https" => Some(443),
            _ => None,
        };

        // Normalise: only store port if it differs from the scheme default
        let port = match (explicit_port, default_port) {
            (Some(p), Some(d)) if p == d => None,
            (p, _) => p,
        };

        Some(Origin {
            scheme: scheme.to_lowercase(),
            host: host_part.to_lowercase(),
            port,
        })
    }

    /// "null" origin (opaque).
    pub fn null() -> Self {
        Origin {
            scheme: "null".into(),
            host: String::new(),
            port: None,
        }
    }

    pub fn is_null(&self) -> bool {
        self.scheme == "null"
    }

    /// Returns true when this origin may access `other` under the same-origin policy.
    pub fn is_same_origin(&self, other: &Origin) -> bool {
        if self.is_null() || other.is_null() {
            return false;
        }
        self.scheme == other.scheme && self.host == other.host && self.port == other.port
    }
}

impl std::fmt::Display for Origin {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}://{}", self.scheme, self.host)?;
        if let Some(p) = self.port {
            write!(f, ":{p}")?;
        }
        Ok(())
    }
}

// ─── Content Security Policy ─────────────────────────────────────────────────

/// Parsed subset of the CSP `script-src` directive relevant to script execution.
#[derive(Debug, Clone, Default)]
pub struct ContentSecurityPolicy {
    /// Inline scripts (`<script>…</script>`) are allowed.
    pub allow_inline: bool,
    /// `eval()` and similar dynamic evaluation are allowed.
    pub allow_eval: bool,
    /// Explicit source list (origins/schemes/keywords).
    pub sources: Vec<String>,
}

impl ContentSecurityPolicy {
    /// Parse a CSP header value and extract script-src rules.
    pub fn from_header(header: &str) -> Self {
        let mut policy = Self::default();

        for directive in header.split(';') {
            let directive = directive.trim();
            let mut parts = directive.split_whitespace();
            let name = match parts.next() {
                Some(n) => n.to_lowercase(),
                None => continue,
            };

            if name != "script-src" && name != "default-src" {
                continue;
            }

            for token in parts {
                match token.to_lowercase().as_str() {
                    "'unsafe-inline'" => policy.allow_inline = true,
                    "'unsafe-eval'" => policy.allow_eval = true,
                    "'none'" => {
                        policy.allow_inline = false;
                        policy.allow_eval = false;
                        policy.sources.clear();
                    }
                    src => policy.sources.push(src.to_string()),
                }
            }
        }

        policy
    }

    /// Returns true when inline scripts are blocked by this policy.
    pub fn blocks_inline(&self) -> bool {
        !self.allow_inline
    }

    /// Returns true when `eval()` is blocked by this policy.
    pub fn blocks_eval(&self) -> bool {
        !self.allow_eval
    }

    /// Returns true when scripts from `origin` are allowed.
    ///
    /// `'self'` only matches if `page_origin` is provided and equals `origin`.
    pub fn allows_origin(&self, origin: &str) -> bool {
        self.sources.iter().any(|s| s == origin || s == "*")
    }

    /// Returns true when `'self'` is in the source list (scripts from the
    /// page's own origin are allowed).
    pub fn allows_self(&self) -> bool {
        self.sources.iter().any(|s| s == "'self'")
    }
}

// ─── Tests ───────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn same_origin_basic() {
        let a = Origin::from_url("https://example.com/page").unwrap();
        let b = Origin::from_url("https://example.com/other").unwrap();
        assert!(a.is_same_origin(&b));
    }

    #[test]
    fn cross_origin_scheme() {
        let a = Origin::from_url("http://example.com").unwrap();
        let b = Origin::from_url("https://example.com").unwrap();
        assert!(!a.is_same_origin(&b));
    }

    #[test]
    fn cross_origin_host() {
        let a = Origin::from_url("https://foo.com").unwrap();
        let b = Origin::from_url("https://bar.com").unwrap();
        assert!(!a.is_same_origin(&b));
    }

    #[test]
    fn cross_origin_port() {
        let a = Origin::from_url("https://example.com:8443").unwrap();
        let b = Origin::from_url("https://example.com").unwrap();
        assert!(!a.is_same_origin(&b));
    }

    #[test]
    fn csp_parse_unsafe_inline() {
        let csp = ContentSecurityPolicy::from_header("script-src 'self' 'unsafe-inline'");
        assert!(csp.allow_inline);
        assert!(!csp.allow_eval);
    }

    #[test]
    fn csp_parse_none_blocks_all() {
        let csp = ContentSecurityPolicy::from_header("script-src 'none'");
        assert!(csp.blocks_inline());
        assert!(csp.blocks_eval());
    }

    #[test]
    fn csp_parse_unsafe_eval() {
        let csp = ContentSecurityPolicy::from_header(
            "default-src 'self'; script-src 'unsafe-eval' https://cdn.example.com",
        );
        assert!(csp.allow_eval);
        assert!(!csp.allow_inline);
        assert!(csp.allows_origin("https://cdn.example.com"));
    }
}
