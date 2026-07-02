use std::fmt;

pub use ::url::Url as InnerUrl;

#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct Url {
    inner: InnerUrl,
}

impl Url {
    pub fn parse(input: &str) -> Result<Self, String> {
        InnerUrl::parse(input)
            .map(|inner| Url { inner })
            .map_err(|e| e.to_string())
    }

    pub fn resolve(base: &Url, relative: &str) -> Result<Self, String> {
        base.inner
            .join(relative)
            .map(|inner| Url { inner })
            .map_err(|e| e.to_string())
    }

    pub fn scheme(&self) -> &str {
        self.inner.scheme()
    }

    pub fn host(&self) -> Option<&str> {
        self.inner.host_str()
    }

    pub fn port(&self) -> Option<u16> {
        self.inner.port()
    }

    pub fn port_or_known_default(&self) -> Option<u16> {
        self.inner.port_or_known_default()
    }

    pub fn path(&self) -> &str {
        self.inner.path()
    }

    pub fn query(&self) -> Option<&str> {
        self.inner.query()
    }

    pub fn fragment(&self) -> Option<&str> {
        self.inner.fragment()
    }

    pub fn origin(&self) -> String {
        let scheme = self.inner.scheme();
        let host = self.inner.host_str().unwrap_or("");
        match self.inner.port() {
            Some(port) => format!("{scheme}://{host}:{port}"),
            None => format!("{scheme}://{host}"),
        }
    }

    pub fn is_same_origin(&self, other: &Url) -> bool {
        self.inner.scheme() == other.inner.scheme()
            && self.inner.host_str() == other.inner.host_str()
            && self.inner.port_or_known_default() == other.inner.port_or_known_default()
    }

    pub fn as_str(&self) -> &str {
        self.inner.as_str()
    }

    pub fn domain(&self) -> Option<&str> {
        self.inner.domain()
    }

    pub fn into_inner(self) -> InnerUrl {
        self.inner
    }
}

impl fmt::Display for Url {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.inner)
    }
}

impl From<InnerUrl> for Url {
    fn from(inner: InnerUrl) -> Self {
        Url { inner }
    }
}

impl TryFrom<&str> for Url {
    type Error = String;
    fn try_from(s: &str) -> Result<Self, Self::Error> {
        Url::parse(s)
    }
}
