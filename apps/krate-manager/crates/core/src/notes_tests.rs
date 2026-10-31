//! `notes` unit tests (plan §7.1 row 011; spec SC-011 as amended by plan SI-9, SI-17, PD-8, PD-9).

use super::*;
use crate::feed;
use std::fs;
use std::path::Path;

fn text(s: &str) -> Inline {
    Inline::Text { text: s.to_owned() }
}

fn para(inlines: Vec<Inline>) -> Block {
    Block::Paragraph { inlines }
}

fn unordered(items: Vec<Vec<Block>>) -> Block {
    Block::List {
        ordered: false,
        start: None,
        items,
    }
}

fn emphasis(inlines: Vec<Inline>) -> Inline {
    Inline::Emphasis { inlines }
}

/// SC-011's 9-deep list: level `k` is indented `2 * (k - 1)` spaces; the ninth level holds two items.
fn nine_deep_list() -> (String, Vec<Block>) {
    let mut md = String::new();
    for k in 1..=8 {
        md.push_str(&" ".repeat(2 * (k - 1)));
        md.push_str(&format!("- l{k}\n"));
    }
    let ninth = " ".repeat(16);
    md.push_str(&format!("{ninth}- l9a\n{ninth}- l9b\n"));
    // Eight `List` levels; the ninth level's text joins the eighth level's item as one `Paragraph` (PD-8).
    let mut item = vec![para(vec![text("l8")]), para(vec![text("l9a l9b")])];
    for k in (1..=7).rev() {
        item = vec![para(vec![text(&format!("l{k}"))]), unordered(vec![item])];
    }
    (md, vec![unordered(vec![item])])
}

/// SC-011's 9-deep block quote (`>` ×9): eight `Quote` levels around one `Paragraph` (PD-8).
fn nine_deep_quote() -> (String, Vec<Block>) {
    let md = format!("{}q", "> ".repeat(9));
    let mut blocks = vec![para(vec![text("q")])];
    for _ in 0..8 {
        blocks = vec![Block::Quote { blocks }];
    }
    (md, blocks)
}

/// SC-011's 9-deep emphasis, alternating `*` and `_`, flattened at inline depth 8: eight `Emphasis` levels, the
/// ninth level's word merged into the eighth level's `Text`. Each opener sits after a space and before a letter
/// (`*a _b *c … *x`): adjacent single delimiters (`*_*_x_*_*`) are both left- and right-flanking in CommonMark, so
/// they close each other early instead of nesting nine deep.
fn nine_deep_emphasis() -> (String, Vec<Block>) {
    let letters = ["a", "b", "c", "d", "e", "f", "g", "h", "x"];
    let delimiter = |k: usize| if k.is_multiple_of(2) { '*' } else { '_' };
    let mut md = String::new();
    for (k, letter) in letters.iter().enumerate() {
        if k > 0 {
            md.push(' ');
        }
        md.push(delimiter(k));
        md.push_str(letter);
    }
    for k in (0..letters.len()).rev() {
        md.push(delimiter(k));
    }
    let mut inner = emphasis(vec![text("h x")]);
    for letter in letters[..7].iter().rev() {
        inner = emphasis(vec![text(&format!("{letter} ")), inner]);
    }
    (md, vec![para(vec![inner])])
}

/// Alpha's `notes_markdown` from the committed `notes-14000.json`, read in place through `feed::parse`.
fn notes_14000_alpha() -> String {
    // crates/core -> crates -> krate-manager -> apps -> the repo root
    let path = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../../../tools/feed-contract/fixtures/feeds/valid/notes-14000.json");
    let bytes = fs::read(&path).unwrap_or_else(|e| panic!("read {}: {e}", path.display()));
    let parsed = feed::parse(&bytes).unwrap_or_else(|e| panic!("parse {}: {e:?}", path.display()));
    parsed
        .plugins
        .get("Alpha")
        .and_then(|entry| entry.release.as_ref())
        .and_then(|release| release.notes_markdown.clone())
        .expect("notes-14000.json: Alpha has notes_markdown")
}

#[test]
fn blocks_cases() {
    let (deep_list_md, deep_list) = nine_deep_list();
    let (deep_quote_md, deep_quote) = nine_deep_quote();
    let (deep_em_md, deep_em) = nine_deep_emphasis();
    let at_bound = "a".repeat(MAX_NOTES_BYTES);
    let over_bound = "a".repeat(MAX_NOTES_BYTES + 1);

    let cases: Vec<(&str, &str, Vec<Block>)> = vec![
        (
            "alpha fixture note",
            "### Fixed\n\n- A fixture note.",
            vec![
                Block::Heading {
                    level: 3,
                    inlines: vec![text("Fixed")],
                },
                unordered(vec![vec![para(vec![text("A fixture note.")])]]),
            ],
        ),
        (
            "strong, text and inline code",
            "**bold** — text with `a_b()`",
            vec![para(vec![
                Inline::Strong {
                    inlines: vec![text("bold")],
                },
                text(" — text with "),
                Inline::Code {
                    text: "a_b()".to_owned(),
                },
            ])],
        ),
        (
            "fenced code block verbatim",
            "```rust\nfn main() {}\n  let *x* = 1;\n```",
            vec![Block::Code {
                text: "fn main() {}\n  let *x* = 1;\n".to_owned(),
            }],
        ),
        (
            "ordered list starting at 3",
            "3. three\n4. four",
            vec![Block::List {
                ordered: true,
                start: Some(3),
                items: vec![
                    vec![para(vec![text("three")])],
                    vec![para(vec![text("four")])],
                ],
            }],
        ),
        (
            "nested lists",
            "- a\n  - b\n- c",
            vec![unordered(vec![
                vec![
                    para(vec![text("a")]),
                    unordered(vec![vec![para(vec![text("b")])]]),
                ],
                vec![para(vec![text("c")])],
            ])],
        ),
        (
            "link keeps its text only",
            "[x](https://e.example)",
            vec![para(vec![text("x")])],
        ),
        (
            "image keeps its alt text only",
            "![alt](https://e.example/i.png)",
            vec![para(vec![text("alt")])],
        ),
        (
            "inline html is literal text",
            "<b>x</b>",
            vec![para(vec![text("<b>x</b>")])],
        ),
        (
            "hard break",
            "a  \nb",
            vec![para(vec![text("a"), Inline::Break, text("b")])],
        ),
        ("9-deep list flattened", deep_list_md.as_str(), deep_list),
        ("9-deep quote flattened", deep_quote_md.as_str(), deep_quote),
        ("9-deep emphasis flattened", deep_em_md.as_str(), deep_em),
        (
            "65,536 bytes is parsed",
            at_bound.as_str(),
            vec![para(vec![text(&at_bound)])],
        ),
        (
            "65,537 bytes is too long",
            over_bound.as_str(),
            vec![para(vec![text(TOO_LONG)])],
        ),
        (
            "html block, final line break removed",
            "<div>\nx\n</div>",
            vec![para(vec![text("<div>\nx\n</div>")])],
        ),
        (
            "thematic break",
            "a\n\n---\n\nb",
            vec![para(vec![text("a")]), Block::Rule, para(vec![text("b")])],
        ),
        (
            "soft break is one merged space",
            "a\nb",
            vec![para(vec![text("a b")])],
        ),
        (
            "ordered list starting at 1 has no start",
            "1. x",
            vec![Block::List {
                ordered: true,
                start: None,
                items: vec![vec![para(vec![text("x")])]],
            }],
        ),
        ("empty", "", vec![]),
        ("whitespace only", "  \n\t\n   ", vec![]),
    ];

    assert_eq!(over_bound.len(), 65_537);
    for (name, markdown, want) in &cases {
        let got = blocks(markdown);
        assert_eq!(&got, want, "case {name}");
        let json = serde_json::to_string(&got).expect("serialise");
        assert!(
            !json.contains("http"),
            "case {name}: serialised model holds a URL: {json}"
        );
    }

    let alpha = notes_14000_alpha();
    let got = blocks(&alpha);
    assert!(
        !got.is_empty(),
        "notes-14000.json: Alpha's notes parse to an empty model"
    );
    let json = serde_json::to_string(&got).expect("serialise");
    assert!(
        !json.contains("http"),
        "notes-14000.json: serialised model holds a URL"
    );
}
