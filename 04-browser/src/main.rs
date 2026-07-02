//! agent-browser — Stage 02: DOM Tree
//!
//! Usage:
//!   agent-browser parse [FILE]                  Parse HTML from file or stdin
//!   agent-browser accessibility [FILE]          Show accessibility tree
//!   agent-browser select "selector" [FILE]      Query DOM with CSS selector
//!   agent-browser tokens [FILE]                 Show raw token stream
//!   agent-browser --help                        Show this help

mod html;
mod dom;

use std::env;
use std::fs;
use std::io::{self, Read};
use std::process;

use html::{build_access_tree, parse, tokenize};
use dom::{build_enhanced_access_tree, query_selector_all};
use html::dom::DOCUMENT_NODE_ID;

fn main() {
    let args: Vec<String> = env::args().collect();
    let prog = args.first().map(|s| s.as_str()).unwrap_or("agent-browser");

    let mut subcommand: Option<&str> = None;
    let mut selector: Option<String> = None;
    let mut file: Option<String> = None;
    let mut accessibility = false;
    let mut _show_tokens = false;
    let mut show_errors = false;
    let mut i = 1;

    while i < args.len() {
        match args[i].as_str() {
            "--help" | "-h" => { print_help(prog); return; }
            "--url" => {
                i += 1;
                // URL support is Stage 03
                eprintln!("--url support is not yet implemented (Stage 03: Network).");
                process::exit(1);
            }
            "--accessibility" | "-a" => { accessibility = true; }
            "--tokens" | "-t"        => { _show_tokens = true; }
            "--errors" | "-e"        => { show_errors = true; }
            s if !s.starts_with('-') && subcommand.is_none() => {
                subcommand = Some(&args[i]);
            }
            s if !s.starts_with('-') && selector.is_none()
                && subcommand == Some("select") =>
            {
                selector = Some(args[i].clone());
            }
            s if !s.starts_with('-') && file.is_none() => {
                file = Some(args[i].clone());
            }
            _ => {}
        }
        i += 1;
    }

    // When the subcommand is "select", the next positional is the selector,
    // then the optional file. Re-parse with that understanding.
    let args_tail: Vec<&str> = args[1..].iter().map(|s| s.as_str()).collect();
    let subcmd = subcommand.unwrap_or("parse");

    match subcmd {
        "select" | "sel" => {
            // Usage: agent-browser select "SELECTOR" [FILE]
            // Positional args after "select" subcommand
            let positional: Vec<&str> = args_tail.iter()
                .filter(|&&s| !s.starts_with('-') && s != "select" && s != "sel")
                .copied()
                .collect();
            if positional.is_empty() {
                eprintln!("Usage: {prog} select \"SELECTOR\" [FILE]");
                process::exit(1);
            }
            let sel_str = positional[0];
            let html_file = positional.get(1).copied();
            let html = read_input(html_file);
            let doc = parse(&html);
            match query_selector_all(&doc, DOCUMENT_NODE_ID, sel_str) {
                Err(e) => {
                    eprintln!("Selector error: {e}");
                    process::exit(1);
                }
                Ok(ids) => {
                    if ids.is_empty() {
                        println!("(no matches for {:?})", sel_str);
                    } else {
                        println!("{} match(es) for {:?}:", ids.len(), sel_str);
                        for id in &ids {
                            let node = doc.node(*id);
                            let tag = node.tag_name().unwrap_or("?");
                            let text_snippet: String = doc.text_content(*id)
                                .trim().chars().take(60).collect();
                            println!("  [node {id}] <{tag}> \"{text_snippet}\"");
                        }
                    }
                    if show_errors && !doc.errors.is_empty() {
                        eprintln!("\n--- Parse errors ({}) ---", doc.errors.len());
                        for e in &doc.errors { eprintln!("  {e}"); }
                    }
                }
            }
        }

        "parse" | "p" => {
            let html = read_input(file.as_deref());
            let doc = parse(&html);
            if show_errors && !doc.errors.is_empty() {
                eprintln!("\n--- Parse errors ({}) ---", doc.errors.len());
                for e in &doc.errors { eprintln!("  {e}"); }
            }
            print!("{doc}");
        }

        "accessibility" | "access" | "ax" => {
            let html = read_input(file.as_deref());
            let doc = parse(&html);
            if accessibility {
                // Enhanced tree
                let tree = build_enhanced_access_tree(&doc);
                print!("{tree}");
            } else {
                let tree = build_access_tree(&doc);
                print!("{tree}");
            }
            if show_errors && !doc.errors.is_empty() {
                eprintln!("\n--- Parse errors ({}) ---", doc.errors.len());
                for e in &doc.errors { eprintln!("  {e}"); }
            }
        }

        "tokens" | "tok" => {
            let html = read_input(file.as_deref());
            let (tokens, errors) = tokenize(&html);
            for (i, tok) in tokens.iter().enumerate() {
                println!("{i:4}: {tok:?}");
            }
            if !errors.is_empty() {
                eprintln!("\n--- Tokenizer errors ({}) ---", errors.len());
                for e in &errors { eprintln!("  {e}"); }
            }
        }

        "--accessibility" | "-a" => {
            let html = read_input(file.as_deref());
            let doc = parse(&html);
            let tree = build_access_tree(&doc);
            print!("{tree}");
        }

        other => {
            // Treat as a file path (parse it)
            let html = read_input(Some(other));
            let doc = parse(&html);
            if accessibility {
                let tree = build_access_tree(&doc);
                print!("{tree}");
            } else {
                print!("{doc}");
            }
        }
    }
}

fn read_input(file: Option<&str>) -> String {
    match file {
        Some(path) => {
            fs::read_to_string(path).unwrap_or_else(|e| {
                eprintln!("Error reading '{}': {e}", path);
                process::exit(1);
            })
        }
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

fn print_help(prog: &str) {
    println!("agent-browser — Stage 02: DOM Tree");
    println!();
    println!("USAGE:");
    println!("  {prog} parse [FILE]                   Parse HTML from file or stdin");
    println!("  {prog} accessibility [FILE]            Show accessibility tree");
    println!("  {prog} select \"SELECTOR\" [FILE]        Query DOM with CSS selector");
    println!("  {prog} tokens [FILE]                   Show raw token stream");
    println!("  cat file.html | {prog} parse           Parse from stdin");
    println!();
    println!("OPTIONS:");
    println!("  --accessibility     Enhanced accessibility tree (with interactive elements)");
    println!("  --errors            Show parse errors alongside output");
    println!("  --tokens            Show raw tokens");
    println!("  --help              Show this help");
    println!();
    println!("SELECTOR EXAMPLES:");
    println!("  {prog} select 'div.content > p' index.html");
    println!("  {prog} select 'a[href]' index.html");
    println!("  {prog} select 'input[type=text]' form.html");
    println!("  {prog} select ':nth-child(2)' index.html");
    println!();
    println!("EXAMPLES:");
    println!("  {prog} parse index.html");
    println!("  {prog} accessibility index.html");
    println!("  echo '<h1>Hello</h1>' | {prog} select 'h1'");
}
