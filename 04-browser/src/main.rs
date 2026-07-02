//! agent-browser — Stage 05: Agent API
//!
//! Usage:
//!   agent-browser fetch <URL>                        Fetch URL, print status/headers/body length
//!   agent-browser fetch <URL> --parse                Fetch URL, parse HTML, print DOM
//!   agent-browser fetch <URL> --accessibility        Fetch URL, parse HTML, print accessibility tree
//!   agent-browser parse [FILE]                       Parse HTML from file or stdin
//!   agent-browser accessibility [FILE]               Show accessibility tree
//!   agent-browser select "selector" [FILE]           Query DOM with CSS selector
//!   agent-browser tokens [FILE]                      Show raw token stream
//!   agent-browser cdp [--port N]                     Start CDP WebSocket server (default 9222)
//!   agent-browser eval SCRIPT                        Evaluate JS and print result
//!   agent-browser inspect                            Print CDP endpoint URL
//!   agent-browser agent <URL> --extract-links        Extract all links
//!   agent-browser agent <URL> --extract-tables       Extract all tables
//!   agent-browser agent <URL> --extract-text         Extract visible text
//!   agent-browser agent <URL> --metadata             Get page metadata
//!   agent-browser agent <URL> --query <selector>     Query elements
//!   agent-browser agent <URL> --click <selector>     Simulate clicking element
//!   agent-browser agent <URL> --type <sel> <text>    Simulate typing into element
//!   agent-browser --help                             Show this help

mod html;
mod dom;
mod net;
mod js;
mod agent;

use std::env;
use std::fs;
use std::io::{self, Read};
use std::process;

use html::{build_access_tree, parse, tokenize};
use dom::{build_enhanced_access_tree, query_selector_all};
use html::dom::DOCUMENT_NODE_ID;
use net::{CookieJar, HttpClient, Url};
use js::engine::{JsEngine, QuickJsEngine};
use js::cdp::CdpServer;
use agent::Page;

#[tokio::main]
async fn main() {
    let args: Vec<String> = env::args().collect();
    let prog = args.first().map(|s| s.as_str()).unwrap_or("agent-browser");

    if args.len() < 2 {
        print_help(prog);
        return;
    }

    match args[1].as_str() {
        "--help" | "-h" => print_help(prog),
        "fetch" => run_fetch(&args[2..], prog).await,
        "parse" | "p" => run_parse(&args[2..]),
        "accessibility" | "access" | "ax" => run_accessibility(&args[2..]),
        "select" | "sel" => run_select(&args[2..], prog),
        "tokens" | "tok" => run_tokens(&args[2..]),
        "cdp" => run_cdp(&args[2..]).await,
        "eval" => run_eval(&args[2..], prog),
        "inspect" => run_inspect(&args[2..]),
        "agent" => run_agent(&args[2..], prog).await,
        other if !other.starts_with('-') => {
            let html = read_file_input(Some(other));
            let doc = parse(&html);
            print!("{doc}");
        }
        _ => print_help(prog),
    }
}

// ─── Stage 05: Agent API commands ────────────────────────────────────────────

async fn run_agent(args: &[String], prog: &str) {
    // First positional arg is the URL.
    let mut url_str: Option<&str> = None;
    let mut do_links = false;
    let mut do_tables = false;
    let mut do_text = false;
    let mut do_metadata = false;
    let mut query_sel: Option<&str> = None;
    let mut click_sel: Option<&str> = None;
    let mut type_sel: Option<&str> = None;
    let mut type_text: Option<&str> = None;

    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--extract-links"   => do_links = true,
            "--extract-tables"  => do_tables = true,
            "--extract-text"    => do_text = true,
            "--metadata"        => do_metadata = true,
            "--query" => {
                i += 1;
                query_sel = args.get(i).map(|s| s.as_str());
            }
            "--click" => {
                i += 1;
                click_sel = args.get(i).map(|s| s.as_str());
            }
            "--type" => {
                i += 1;
                type_sel  = args.get(i).map(|s| s.as_str());
                i += 1;
                type_text = args.get(i).map(|s| s.as_str());
            }
            s if !s.starts_with('-') && url_str.is_none() => url_str = Some(s),
            _ => {}
        }
        i += 1;
    }

    let url_str = url_str.unwrap_or_else(|| {
        eprintln!("Usage: {prog} agent <URL> [--extract-links|--extract-tables|--extract-text|--metadata|--query SEL|--click SEL|--type SEL TEXT]");
        process::exit(1);
    });

    let mut page = Page::new();
    if let Err(e) = page.goto(url_str).await {
        eprintln!("Error fetching {url_str}: {e}");
        process::exit(1);
    }

    let mut did_something = false;

    if do_metadata {
        did_something = true;
        let m = page.extract_metadata();
        println!("Title:       {}", m.title);
        println!("URL:         {}", page.url().unwrap_or("(none)"));
        if let Some(d) = &m.description { println!("Description: {d}"); }
        if let Some(t) = &m.og_title   { println!("og:title:    {t}"); }
        if let Some(d) = &m.og_description { println!("og:desc:     {d}"); }
        if let Some(u) = &m.canonical_url  { println!("Canonical:   {u}"); }
        println!();
    }

    if do_links {
        did_something = true;
        let links = page.extract_links();
        println!("Links ({}):", links.len());
        for (text, href) in &links {
            println!("  [{text}] -> {href}");
        }
        println!();
    }

    if do_tables {
        did_something = true;
        let rows = page.extract_table("table");
        if rows.is_empty() {
            println!("(no tables found)");
        } else {
            println!("Table ({} rows):", rows.len());
            for row in &rows {
                println!("  {}", row.join(" | "));
            }
        }
        println!();
    }

    if do_text {
        did_something = true;
        println!("{}", page.extract_text());
        println!();
    }

    if let Some(sel) = query_sel {
        did_something = true;
        let elements = page.query_all(sel);
        println!("{} element(s) for {sel:?}:", elements.len());
        for el in &elements {
            let tag  = el.tag_name().unwrap_or_else(|| "?".to_string());
            let text: String = el.text_content().trim().chars().take(60).collect();
            println!("  <{tag}> {:?}", text);
        }
        println!();
    }

    if let Some(sel) = click_sel {
        did_something = true;
        use agent::Action;
        match page.execute(Action::Click(sel.to_string())) {
            Ok(msg) => println!("{msg}"),
            Err(e)  => eprintln!("click error: {e}"),
        }
    }

    if let (Some(sel), Some(text)) = (type_sel, type_text) {
        did_something = true;
        use agent::Action;
        match page.execute(Action::Type(sel.to_string(), text.to_string())) {
            Ok(msg) => println!("{msg}"),
            Err(e)  => eprintln!("type error: {e}"),
        }
    }

    if !did_something {
        // Default: print metadata + link count.
        let m = page.extract_metadata();
        println!("Title: {}", m.title);
        println!("URL:   {}", page.url().unwrap_or("(none)"));
        let links = page.extract_links();
        println!("Links: {}", links.len());
        let text = page.extract_text();
        let snippet: String = text.chars().take(200).collect();
        println!("Text:  {snippet}…");
    }
}

// ─── Stage 04 commands ────────────────────────────────────────────────────────

async fn run_cdp(args: &[String]) {
    let mut port: u16 = 9222;
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--port" | "-p" => {
                i += 1;
                if let Some(p) = args.get(i) {
                    port = p.parse().unwrap_or_else(|_| {
                        eprintln!("Invalid port: {p}");
                        process::exit(1);
                    });
                }
            }
            _ => {}
        }
        i += 1;
    }

    let addr = format!("127.0.0.1:{port}");
    let server = CdpServer::bind(&addr).await.unwrap_or_else(|e| {
        eprintln!("Failed to start CDP server on {addr}: {e}");
        process::exit(1);
    });

    println!("CDP server listening on {}", server.debugger_url());
    println!("Connect with: playwright chromium.connectOverCDP(\"{}\")", server.debugger_url());
    server.run().await;
}

fn run_eval(args: &[String], prog: &str) {
    let script = args.first().unwrap_or_else(|| {
        eprintln!("Usage: {prog} eval SCRIPT");
        process::exit(1);
    });

    let engine = QuickJsEngine::new();
    let mut ctx = engine.create_context().unwrap_or_else(|e| {
        eprintln!("Failed to create JS context: {e}");
        process::exit(1);
    });

    match ctx.eval(script) {
        Ok(val) => println!("{val}"),
        Err(e) => {
            eprintln!("JS error: {e}");
            if let Some(stack) = &e.stack {
                eprintln!("{stack}");
            }
            process::exit(1);
        }
    }
}

fn run_inspect(args: &[String]) {
    let port: u16 = args.iter()
        .position(|a| a == "--port" || a == "-p")
        .and_then(|i| args.get(i + 1))
        .and_then(|p| p.parse().ok())
        .unwrap_or(9222);
    println!("ws://127.0.0.1:{port}");
    println!("HTTP: http://127.0.0.1:{port}/json/version");
}

// ─── Stage 01–03 commands (unchanged) ────────────────────────────────────────

async fn run_fetch(args: &[String], prog: &str) {
    let mut url_str: Option<&str> = None;
    let mut do_parse = false;
    let mut do_accessibility = false;
    let mut show_errors = false;

    for arg in args {
        match arg.as_str() {
            "--parse" | "-p" => do_parse = true,
            "--accessibility" | "-a" => do_accessibility = true,
            "--errors" | "-e" => show_errors = true,
            s if !s.starts_with('-') && url_str.is_none() => url_str = Some(s),
            _ => {}
        }
    }

    let url_str = url_str.unwrap_or_else(|| {
        eprintln!("Usage: {prog} fetch <URL> [--parse] [--accessibility]");
        process::exit(1);
    });

    let url = Url::parse(url_str).unwrap_or_else(|e| {
        eprintln!("Invalid URL '{url_str}': {e}");
        process::exit(1);
    });

    let client = HttpClient::with_defaults();
    let mut jar = CookieJar::new();

    let response = client.get(&url, &mut jar).await.unwrap_or_else(|e| {
        eprintln!("Fetch error: {e}");
        process::exit(1);
    });

    println!("Status:       {} {}", response.status, status_text(response.status));
    println!("URL:          {}", response.final_url);
    println!("Redirects:    {}", response.redirect_count);
    println!("Body length:  {} bytes", response.body.len());
    println!("Content-Type: {:?}", response.content_type);
    println!();

    let interesting_headers = ["content-type", "content-length", "server", "date", "cache-control", "etag"];
    for h in &interesting_headers {
        if let Some(v) = response.header(h) {
            println!("  {h}: {v}");
        }
    }

    if do_accessibility || do_parse {
        let body_str = response.body_as_str();
        let doc = parse(body_str);

        if show_errors && !doc.errors.is_empty() {
            eprintln!("\n--- Parse errors ({}) ---", doc.errors.len());
            for e in &doc.errors {
                eprintln!("  {e}");
            }
        }

        println!();
        if do_accessibility {
            let tree = build_access_tree(&doc);
            print!("{tree}");
        } else {
            print!("{doc}");
        }
    }
}

fn run_parse(args: &[String]) {
    let mut file: Option<&str> = None;
    let mut show_errors = false;

    for arg in args {
        match arg.as_str() {
            "--errors" | "-e" => show_errors = true,
            s if !s.starts_with('-') && file.is_none() => file = Some(s),
            _ => {}
        }
    }

    let html = read_file_input(file);
    let doc = parse(&html);
    if show_errors && !doc.errors.is_empty() {
        eprintln!("\n--- Parse errors ({}) ---", doc.errors.len());
        for e in &doc.errors {
            eprintln!("  {e}");
        }
    }
    print!("{doc}");
}

fn run_accessibility(args: &[String]) {
    let mut file: Option<&str> = None;
    let mut enhanced = false;
    let mut show_errors = false;

    for arg in args {
        match arg.as_str() {
            "--enhanced" | "--accessibility" | "-a" => enhanced = true,
            "--errors" | "-e" => show_errors = true,
            s if !s.starts_with('-') && file.is_none() => file = Some(s),
            _ => {}
        }
    }

    let html = read_file_input(file);
    let doc = parse(&html);

    if show_errors && !doc.errors.is_empty() {
        eprintln!("\n--- Parse errors ({}) ---", doc.errors.len());
        for e in &doc.errors {
            eprintln!("  {e}");
        }
    }

    if enhanced {
        let tree = build_enhanced_access_tree(&doc);
        print!("{tree}");
    } else {
        let tree = build_access_tree(&doc);
        print!("{tree}");
    }
}

fn run_select(args: &[String], prog: &str) {
    let mut selector: Option<&str> = None;
    let mut file: Option<&str> = None;
    let mut show_errors = false;

    for arg in args {
        match arg.as_str() {
            "--errors" | "-e" => show_errors = true,
            s if !s.starts_with('-') && selector.is_none() => selector = Some(s),
            s if !s.starts_with('-') && file.is_none() => file = Some(s),
            _ => {}
        }
    }

    let sel_str = selector.unwrap_or_else(|| {
        eprintln!("Usage: {prog} select \"SELECTOR\" [FILE]");
        process::exit(1);
    });

    let html = read_file_input(file);
    let doc = parse(&html);

    match query_selector_all(&doc, DOCUMENT_NODE_ID, sel_str) {
        Err(e) => {
            eprintln!("Selector error: {e}");
            process::exit(1);
        }
        Ok(ids) => {
            if ids.is_empty() {
                println!("(no matches for {sel_str:?})");
            } else {
                println!("{} match(es) for {sel_str:?}:", ids.len());
                for id in &ids {
                    let node = doc.node(*id);
                    let tag = node.tag_name().unwrap_or("?");
                    let text_snippet: String =
                        doc.text_content(*id).trim().chars().take(60).collect();
                    println!("  [node {id}] <{tag}> \"{text_snippet}\"");
                }
            }
            if show_errors && !doc.errors.is_empty() {
                eprintln!("\n--- Parse errors ({}) ---", doc.errors.len());
                for e in &doc.errors {
                    eprintln!("  {e}");
                }
            }
        }
    }
}

fn run_tokens(args: &[String]) {
    let mut file: Option<&str> = None;
    for arg in args {
        if !arg.starts_with('-') && file.is_none() {
            file = Some(arg.as_str());
        }
    }
    let html = read_file_input(file);
    let (tokens, errors) = tokenize(&html);
    for (i, tok) in tokens.iter().enumerate() {
        println!("{i:4}: {tok:?}");
    }
    if !errors.is_empty() {
        eprintln!("\n--- Tokenizer errors ({}) ---", errors.len());
        for e in &errors {
            eprintln!("  {e}");
        }
    }
}

fn read_file_input(file: Option<&str>) -> String {
    match file {
        Some(path) => fs::read_to_string(path).unwrap_or_else(|e| {
            eprintln!("Error reading '{path}': {e}");
            process::exit(1);
        }),
        None => {
            let mut buf = String::new();
            io::stdin().read_to_string(&mut buf).unwrap_or_else(|e| {
                eprintln!("Error reading stdin: {e}");
                process::exit(1);
            });
            buf
        }
    }
}

fn status_text(code: u16) -> &'static str {
    match code {
        200 => "OK", 201 => "Created", 204 => "No Content",
        301 => "Moved Permanently", 302 => "Found", 303 => "See Other",
        304 => "Not Modified", 307 => "Temporary Redirect", 308 => "Permanent Redirect",
        400 => "Bad Request", 401 => "Unauthorized", 403 => "Forbidden",
        404 => "Not Found", 405 => "Method Not Allowed", 429 => "Too Many Requests",
        500 => "Internal Server Error", 502 => "Bad Gateway", 503 => "Service Unavailable",
        _ => "",
    }
}

fn print_help(prog: &str) {
    println!("agent-browser — Stage 05: Agent API");
    println!();
    println!("USAGE:");
    println!("  {prog} fetch <URL>                      Fetch URL, print response info");
    println!("  {prog} fetch <URL> --parse              Fetch and parse HTML, show DOM");
    println!("  {prog} fetch <URL> --accessibility      Fetch and show accessibility tree");
    println!("  {prog} parse [FILE]                     Parse HTML from file or stdin");
    println!("  {prog} accessibility [FILE]             Show accessibility tree");
    println!("  {prog} select \"SELECTOR\" [FILE]         Query DOM with CSS selector");
    println!("  {prog} tokens [FILE]                    Show raw token stream");
    println!("  {prog} cdp [--port N]                       Start CDP server (default port 9222)");
    println!("  {prog} eval SCRIPT                          Evaluate JS expression and print result");
    println!("  {prog} inspect [--port N]                   Print CDP endpoint URL");
    println!("  {prog} agent <URL> --extract-links          Extract all links");
    println!("  {prog} agent <URL> --extract-tables         Extract all tables");
    println!("  {prog} agent <URL> --extract-text           Extract visible text");
    println!("  {prog} agent <URL> --metadata               Get page metadata");
    println!("  {prog} agent <URL> --query <sel>            Query elements by CSS selector");
    println!("  {prog} agent <URL> --click <sel>            Simulate click on element");
    println!("  {prog} agent <URL> --type <sel> <text>      Simulate typing into element");
    println!();
    println!("OPTIONS:");
    println!("  --errors        Show parse errors alongside output");
    println!("  --port N        CDP server port (default 9222)");
    println!("  --help          Show this help");
    println!();
    println!("EXAMPLES:");
    println!("  {prog} cdp");
    println!("  {prog} cdp --port 9333");
    println!("  {prog} eval \"1 + 1\"");
    println!("  {prog} eval \"document.title\"");
    println!("  {prog} inspect");
}
