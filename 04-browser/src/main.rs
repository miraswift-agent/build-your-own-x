//! agent-browser — Stage 03: Network & Protocol
//!
//! Usage:
//!   agent-browser fetch <URL>                   Fetch URL, print status/headers/body length
//!   agent-browser fetch <URL> --parse           Fetch URL, parse HTML, print DOM
//!   agent-browser fetch <URL> --accessibility   Fetch URL, parse HTML, print accessibility tree
//!   agent-browser parse [FILE]                  Parse HTML from file or stdin
//!   agent-browser accessibility [FILE]          Show accessibility tree
//!   agent-browser select "selector" [FILE]      Query DOM with CSS selector
//!   agent-browser tokens [FILE]                 Show raw token stream
//!   agent-browser --help                        Show this help

mod html;
mod dom;
mod net;

use std::env;
use std::fs;
use std::io::{self, Read};
use std::process;

use html::{build_access_tree, parse, tokenize};
use dom::{build_enhanced_access_tree, query_selector_all};
use html::dom::DOCUMENT_NODE_ID;
use net::{CookieJar, HttpClient, Url};

#[tokio::main]
async fn main() {
    let args: Vec<String> = env::args().collect();
    let prog = args.first().map(|s| s.as_str()).unwrap_or("agent-browser");

    if args.len() < 2 {
        print_help(prog);
        return;
    }

    match args[1].as_str() {
        "--help" | "-h" => {
            print_help(prog);
        }
        "fetch" => {
            run_fetch(&args[2..], prog).await;
        }
        "parse" | "p" => {
            run_parse(&args[2..]);
        }
        "accessibility" | "access" | "ax" => {
            run_accessibility(&args[2..]);
        }
        "select" | "sel" => {
            run_select(&args[2..], prog);
        }
        "tokens" | "tok" => {
            run_tokens(&args[2..]);
        }
        other if !other.starts_with('-') => {
            // Treat as file path
            let html = read_file_input(Some(other));
            let doc = parse(&html);
            print!("{doc}");
        }
        _ => {
            print_help(prog);
        }
    }
}

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
        200 => "OK",
        201 => "Created",
        204 => "No Content",
        301 => "Moved Permanently",
        302 => "Found",
        303 => "See Other",
        304 => "Not Modified",
        307 => "Temporary Redirect",
        308 => "Permanent Redirect",
        400 => "Bad Request",
        401 => "Unauthorized",
        403 => "Forbidden",
        404 => "Not Found",
        405 => "Method Not Allowed",
        429 => "Too Many Requests",
        500 => "Internal Server Error",
        502 => "Bad Gateway",
        503 => "Service Unavailable",
        _ => "",
    }
}

fn print_help(prog: &str) {
    println!("agent-browser — Stage 03: Network & Protocol");
    println!();
    println!("USAGE:");
    println!("  {prog} fetch <URL>                      Fetch URL, print response info");
    println!("  {prog} fetch <URL> --parse              Fetch and parse HTML, show DOM");
    println!("  {prog} fetch <URL> --accessibility      Fetch and show accessibility tree");
    println!("  {prog} parse [FILE]                     Parse HTML from file or stdin");
    println!("  {prog} accessibility [FILE]             Show accessibility tree");
    println!("  {prog} select \"SELECTOR\" [FILE]         Query DOM with CSS selector");
    println!("  {prog} tokens [FILE]                    Show raw token stream");
    println!();
    println!("OPTIONS:");
    println!("  --errors        Show parse errors alongside output");
    println!("  --help          Show this help");
    println!();
    println!("EXAMPLES:");
    println!("  {prog} fetch https://example.com");
    println!("  {prog} fetch https://example.com --parse");
    println!("  {prog} fetch https://example.com --accessibility");
    println!("  {prog} parse index.html");
    println!("  {prog} select 'a[href]' index.html");
}
