use crate::html::dom::Document;
use crate::net::cookies::CookieJar;
use crate::net::http::HttpClient;
use crate::net::url::Url;

#[derive(Debug, Clone, PartialEq)]
pub enum ResourceKind {
    Html,
    Css,
    JavaScript,
    Image,
    Font,
    Other,
}

#[derive(Debug, Clone, PartialEq)]
pub enum LoadState {
    Pending,
    Loading,
    Loaded,
    Failed(String),
}

#[derive(Debug)]
pub struct Resource {
    pub url: Url,
    pub kind: ResourceKind,
    pub state: LoadState,
    pub content: Option<Vec<u8>>,
}

#[derive(Debug, Clone)]
pub struct LoadConfig {
    pub load_css: bool,
    pub load_js: bool,
    pub load_images: bool,
    pub load_fonts: bool,
}

impl Default for LoadConfig {
    fn default() -> Self {
        // Agent-optimized defaults: skip images and fonts
        Self {
            load_css: true,
            load_js: true,
            load_images: false,
            load_fonts: false,
        }
    }
}

pub fn find_resources(doc: &Document, base_url: &Url, config: &LoadConfig) -> Vec<Resource> {
    let mut resources = Vec::new();

    for node in doc.nodes.iter() {
        let Some(elem) = node.element_data() else { continue };
        let tag = elem.tag_name.as_str();

        match tag {
            "link" => {
                let rel = elem.attrs.iter()
                    .find(|a| a.name == "rel")
                    .map(|a| a.value.to_lowercase());
                let href = elem.attrs.iter()
                    .find(|a| a.name == "href")
                    .map(|a| a.value.as_str());

                if let (Some(rel), Some(href)) = (rel, href) {
                    if rel.contains("stylesheet") && config.load_css {
                        if let Ok(url) = Url::resolve(base_url, href) {
                            resources.push(Resource {
                                url,
                                kind: ResourceKind::Css,
                                state: LoadState::Pending,
                                content: None,
                            });
                        }
                    }
                }
            }
            "script" => {
                if config.load_js {
                    let src = elem.attrs.iter()
                        .find(|a| a.name == "src")
                        .map(|a| a.value.as_str());
                    if let Some(src) = src {
                        if let Ok(url) = Url::resolve(base_url, src) {
                            resources.push(Resource {
                                url,
                                kind: ResourceKind::JavaScript,
                                state: LoadState::Pending,
                                content: None,
                            });
                        }
                    }
                }
            }
            "img" => {
                if config.load_images {
                    let src = elem.attrs.iter()
                        .find(|a| a.name == "src")
                        .map(|a| a.value.as_str());
                    if let Some(src) = src {
                        if let Ok(url) = Url::resolve(base_url, src) {
                            resources.push(Resource {
                                url,
                                kind: ResourceKind::Image,
                                state: LoadState::Pending,
                                content: None,
                            });
                        }
                    }
                }
            }
            _ => {}
        }
    }

    resources
}

pub async fn load_resources(
    doc: &Document,
    base_url: &Url,
    client: &HttpClient,
    jar: &mut CookieJar,
    config: &LoadConfig,
) -> Vec<Resource> {
    let mut resources = find_resources(doc, base_url, config);

    for resource in &mut resources {
        resource.state = LoadState::Loading;
        match client.get(&resource.url, jar).await {
            Ok(response) => {
                resource.state = LoadState::Loaded;
                resource.content = Some(response.body);
            }
            Err(e) => {
                resource.state = LoadState::Failed(e);
            }
        }
    }

    resources
}
