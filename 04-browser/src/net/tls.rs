//! TLS configuration helpers for hyper-rustls.

use hyper_rustls::HttpsConnector;
use hyper_util::client::legacy::connect::HttpConnector;

pub fn verified_connector() -> HttpsConnector<HttpConnector> {
    hyper_rustls::HttpsConnectorBuilder::new()
        .with_webpki_roots()
        .https_or_http()
        .enable_http1()
        .enable_http2()
        .build()
}
