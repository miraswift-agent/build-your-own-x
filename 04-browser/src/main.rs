//! agent-browser — Stage 01: HTML Parser
//!
//! Usage:
//!   agent-browser parse [FILE]          Parse HTML from file or stdin
//!   agent-browser parse --url URL       Fetch URL and parse (Stage 03: not yet implemented)
//!   agent-browser accessibility [FILE]  Show accessibility tree
//!   agent-browser tokens [FILE]         Show raw token stream
//!   agent-browser --help                Show this help

mod html;

use std::env;
use std::fs;
use std::io::{self, Read};
use std::process;

use html::{build_access_tree, parse, tokenize};

fn main() {
    let args: Vec<String> = env::args().collect();
    let prog = args.first().map(|s| s.as_str()).unwrap_or("agent-browser");

    // Minimal arg parsing (no external deps in Stage 01)
    let mut subcommand: Option<&str> = None;
    let mut url: Option<String> = None;
    let mut file: Option<String> = None;
    let mut accessibility = false;
    let mut _show_tokens = false;
    let mut show_errors = false;
    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "--help" | "-h" => {
                print_help(prog);
                return;
            }
            "--url" => {
                i += 1;
                if i < args.len() { url = Some(args[i].clone()); }
            }
            "--accessibility" | "-a" => { accessibility = true; }
            "--tokens" | "-t" => { _show_tokens = true; }
            "--errors" | "-e" => { show_errors = true; }
            s if !s.starts_with('-') && subcommand.is_none() => {
                subcommand = Some(&args[i]);
            }
            s if !s.starts_with('-') && file.is_none() => {
                file = Some(args[i].clone());
            }
            _ => {}
        }
        i += 1;
    }

    // Normalise subcommand
    let subcmd = subcommand.unwrap_or("parse");

    match subcmd {
        "parse" | "p" => {
            if url.is_some() {
                eprintln!("--url support is not yet implemented (Stage 03: Network).");
                eprintln!("Provide an HTML file or pipe via stdin instead.");
                process::exit(1);
            }
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
            let tree = build_access_tree(&doc);
            print!("{tree}");
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
            // Support: agent-browser --accessibility < file.html
            let html = read_input(file.as_deref());
            let doc = parse(&html);
            let tree = build_access_tree(&doc);
            print!("{tree}");
        }
        other => {
            // Treat unrecognised subcommand as a file path (parse it)
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
    println!("agent-browser — Stage 01: HTML Parser");
    println!();
    println!("USAGE:");
    println!("  {prog} parse [FILE]              Parse HTML from file or stdin");
    println!("  {prog} accessibility [FILE]       Show accessibility tree");
    println!("  {prog} tokens [FILE]              Show raw token stream");
    println!("  cat file.html | {prog} parse      Parse from stdin");
    println!();
    println!("OPTIONS:");
    println!("  --url URL           Fetch and parse a URL (Stage 03: not yet implemented)");
    println!("  --accessibility     Equivalent to 'accessibility' subcommand");
    println!("  --errors            Show parse errors alongside output");
    println!("  --tokens            Show raw tokens");
    println!("  --help              Show this help");
    println!();
    println!("EXAMPLES:");
    println!("  {prog} parse index.html");
    println!("  {prog} accessibility index.html");
    println!("  echo '<h1>Hello</h1>' | {prog} parse");
}
