//! Release notes as a block model, never HTML (plan §4.7; spec FR-050 to FR-054).
//!
//! `pulldown_cmark` is named only here and in `notes_tests.rs` (FR-082). The builder keeps a stack of block containers
//! (lists, items, quotes) and, inside a paragraph or heading, a stack of inline containers (strong, emphasis). Link and
//! image tags push nothing, so only their child inlines survive: no destination is ever read.

use pulldown_cmark::{Event, Options, Parser, Tag, TagEnd};
use serde::Serialize;

/// Notes longer than this are not parsed (FR-053).
pub const MAX_NOTES_BYTES: usize = 65_536;
/// The deepest nesting kept; deeper content is flattened (FR-053).
pub const MAX_DEPTH: usize = 8;
/// The one paragraph shown for notes over `MAX_NOTES_BYTES` (FR-053).
pub const TOO_LONG: &str = "Release notes are too long to show here.";

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum Block {
    Heading {
        level: u8,
        inlines: Vec<Inline>,
    },
    Paragraph {
        inlines: Vec<Inline>,
    },
    List {
        ordered: bool,
        start: Option<u64>,
        items: Vec<Vec<Block>>,
    },
    Quote {
        blocks: Vec<Block>,
    },
    Code {
        text: String,
    },
    Rule,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum Inline {
    Text { text: String },
    Strong { inlines: Vec<Inline> },
    Emphasis { inlines: Vec<Inline> },
    Code { text: String },
    Break,
}

/// The block model of `markdown` (FR-050 to FR-054).
pub fn blocks(markdown: &str) -> Vec<Block> {
    if markdown.len() > MAX_NOTES_BYTES {
        return vec![Block::Paragraph {
            inlines: vec![Inline::Text {
                text: TOO_LONG.to_owned(),
            }],
        }];
    }
    if markdown.trim().is_empty() {
        return Vec::new();
    }
    let mut builder = Builder::default();
    for event in Parser::new_ext(markdown, Options::empty()) {
        builder.event(event);
    }
    builder.finish()
}

/// An open block container.
enum Container {
    List {
        ordered: bool,
        start: Option<u64>,
        items: Vec<Vec<Block>>,
    },
    Item(Vec<Block>),
    Quote(Vec<Block>),
}

/// What a closed inline vector becomes.
#[derive(Clone, Copy)]
enum InlineOwner {
    Heading(u8),
    /// `implicit`: opened by inline content outside a paragraph (a tight list item), closed at the next block event.
    Paragraph {
        implicit: bool,
    },
}

#[derive(Clone, Copy)]
enum InlineKind {
    Strong,
    Emphasis,
}

/// An open paragraph or heading.
struct InlineLeaf {
    owner: InlineOwner,
    base: Vec<Inline>,
    open: Vec<(InlineKind, Vec<Inline>)>,
    /// Inline containers swallowed past `MAX_DEPTH` and not yet closed (PD-8).
    overflow: usize,
}

impl InlineLeaf {
    fn new(owner: InlineOwner) -> Self {
        Self {
            owner,
            base: Vec::new(),
            open: Vec::new(),
            overflow: 0,
        }
    }

    /// The innermost open inline vector.
    fn target(&mut self) -> &mut Vec<Inline> {
        self.open.last_mut().map_or(&mut self.base, |(_, v)| v)
    }

    /// Appends one event's inline; past `MAX_DEPTH` everything is plain text (PD-8).
    fn push(&mut self, inline: Inline) {
        let inline = match inline {
            Inline::Code { text } if self.overflow > 0 => Inline::Text { text },
            Inline::Break if self.overflow > 0 => Inline::Text {
                text: " ".to_owned(),
            },
            other => other,
        };
        push_inline(self.target(), inline);
    }

    fn open(&mut self, kind: InlineKind) {
        if self.overflow > 0 || self.open.len() >= MAX_DEPTH {
            self.overflow += 1;
        } else {
            self.open.push((kind, Vec::new()));
        }
    }

    fn close(&mut self) {
        if self.overflow > 0 {
            self.overflow -= 1;
        } else if let Some((kind, inlines)) = self.open.pop() {
            push_inline(self.target(), wrap(kind, inlines));
        }
    }

    fn into_block(mut self) -> Block {
        self.overflow = 0;
        while !self.open.is_empty() {
            self.close();
        }
        match self.owner {
            InlineOwner::Heading(level) => Block::Heading {
                level,
                inlines: self.base,
            },
            InlineOwner::Paragraph { .. } => Block::Paragraph { inlines: self.base },
        }
    }
}

/// An open code or HTML block, collecting its text verbatim.
enum RawLeaf {
    Code(String),
    Html(String),
}

/// A block container that would open past `MAX_DEPTH`, collected as plain text until its matching end (PD-8).
struct Overflow {
    depth: usize,
    text: String,
    /// A break or block boundary since the last text: one space before the next text, however many there were.
    pending_space: bool,
}

impl Overflow {
    fn push_text(&mut self, s: &str) {
        if self.pending_space && !self.text.is_empty() {
            self.text.push(' ');
        }
        self.pending_space = false;
        self.text.push_str(s);
    }

    /// Feeds one event; true when the overflowed container has closed.
    fn feed(&mut self, event: &Event<'_>) -> bool {
        match event {
            Event::Start(tag) => {
                self.depth += 1;
                if is_block_end(tag.to_end()) {
                    self.pending_space = true;
                }
            }
            Event::End(end) => {
                self.depth -= 1;
                if is_block_end(*end) {
                    self.pending_space = true;
                }
            }
            Event::Text(s)
            | Event::Code(s)
            | Event::Html(s)
            | Event::InlineHtml(s)
            | Event::InlineMath(s)
            | Event::DisplayMath(s)
            | Event::FootnoteReference(s) => self.push_text(s),
            Event::SoftBreak | Event::HardBreak | Event::Rule => self.pending_space = true,
            Event::TaskListMarker(_) => {}
        }
        self.depth == 0
    }
}

/// A block tag's start or end is a block boundary inside the overflow collector: one space (PD-8).
fn is_block_end(end: TagEnd) -> bool {
    !matches!(
        end,
        TagEnd::Emphasis
            | TagEnd::Strong
            | TagEnd::Strikethrough
            | TagEnd::Superscript
            | TagEnd::Subscript
            | TagEnd::Link
            | TagEnd::Image
    )
}

/// Appends `inline`, merging it into a preceding `Text` when both are text.
fn push_inline(inlines: &mut Vec<Inline>, inline: Inline) {
    if let (Inline::Text { text }, Some(Inline::Text { text: prev })) =
        (&inline, inlines.last_mut())
    {
        prev.push_str(text);
        return;
    }
    inlines.push(inline);
}

fn wrap(kind: InlineKind, inlines: Vec<Inline>) -> Inline {
    match kind {
        InlineKind::Strong => Inline::Strong { inlines },
        InlineKind::Emphasis => Inline::Emphasis { inlines },
    }
}

#[derive(Default)]
struct Builder {
    root: Vec<Block>,
    containers: Vec<Container>,
    inline: Option<InlineLeaf>,
    raw: Option<RawLeaf>,
    overflow: Option<Overflow>,
}

impl Builder {
    fn event(&mut self, event: Event<'_>) {
        if let Some(overflow) = &mut self.overflow {
            if overflow.feed(&event) {
                let text = overflow.text.trim().to_owned();
                self.overflow = None;
                if !text.is_empty() {
                    self.push_block(Block::Paragraph {
                        inlines: vec![Inline::Text { text }],
                    });
                }
            }
            return;
        }
        match event {
            Event::Start(tag) => self.start(tag),
            Event::End(end) => self.end(end),
            Event::Code(s) => self.inline_leaf().push(Inline::Code {
                text: s.into_string(),
            }),
            Event::Text(s)
            | Event::Html(s)
            | Event::InlineHtml(s)
            | Event::InlineMath(s)
            | Event::DisplayMath(s)
            | Event::FootnoteReference(s) => self.text(&s),
            Event::SoftBreak => self.text(" "),
            Event::HardBreak => self.inline_leaf().push(Inline::Break),
            Event::Rule => {
                self.close_implicit();
                self.push_block(Block::Rule);
            }
            Event::TaskListMarker(_) => {}
        }
    }

    /// Lists and quotes open; items are not a level of their own.
    fn block_depth(&self) -> usize {
        self.containers
            .iter()
            .filter(|c| !matches!(c, Container::Item(_)))
            .count()
    }

    /// Opens a list or quote, or starts the overflow collector when it would sit past `MAX_DEPTH` (PD-8).
    fn open_container(&mut self, container: Container) {
        self.close_implicit();
        if self.block_depth() >= MAX_DEPTH {
            self.overflow = Some(Overflow {
                depth: 1,
                text: String::new(),
                pending_space: false,
            });
        } else {
            self.containers.push(container);
        }
    }

    fn start(&mut self, tag: Tag<'_>) {
        match tag {
            Tag::Paragraph => self.open_inline_leaf(InlineOwner::Paragraph { implicit: false }),
            // `HeadingLevel` has explicit discriminants 1 to 6.
            Tag::Heading { level, .. } => self.open_inline_leaf(InlineOwner::Heading(level as u8)),
            Tag::BlockQuote(_) => self.open_container(Container::Quote(Vec::new())),
            Tag::List(first) => self.open_container(Container::List {
                ordered: first.is_some(),
                start: first.filter(|&n| n != 1),
                items: Vec::new(),
            }),
            Tag::Item => {
                self.close_implicit();
                self.containers.push(Container::Item(Vec::new()));
            }
            Tag::CodeBlock(_) => {
                self.close_implicit();
                self.raw = Some(RawLeaf::Code(String::new()));
            }
            Tag::HtmlBlock => {
                self.close_implicit();
                self.raw = Some(RawLeaf::Html(String::new()));
            }
            Tag::Strong => self.inline_leaf().open(InlineKind::Strong),
            Tag::Emphasis => self.inline_leaf().open(InlineKind::Emphasis),
            // Links and images push nothing: their children flow into the parent and the destination is never read
            // (FR-052). No other tag occurs under `Options::empty()`; its children flow the same way.
            _ => {}
        }
    }

    fn end(&mut self, end: TagEnd) {
        match end {
            TagEnd::Paragraph | TagEnd::Heading(_) => self.close_inline_leaf(),
            TagEnd::CodeBlock | TagEnd::HtmlBlock => self.close_raw(),
            TagEnd::BlockQuote(_) | TagEnd::List(_) | TagEnd::Item => {
                self.close_implicit();
                self.close_container();
            }
            TagEnd::Strong | TagEnd::Emphasis => {
                if let Some(leaf) = &mut self.inline {
                    leaf.close();
                }
            }
            _ => {}
        }
    }

    fn finish(mut self) -> Vec<Block> {
        self.close_inline_leaf();
        self.close_raw();
        while !self.containers.is_empty() {
            self.close_container();
        }
        self.root
    }

    /// Appends to the innermost block vector: the open item or quote, a list's last item, or the root.
    fn push_block(&mut self, block: Block) {
        match self.containers.last_mut() {
            None => self.root.push(block),
            Some(Container::Item(blocks) | Container::Quote(blocks)) => blocks.push(block),
            Some(Container::List { items, .. }) => match items.last_mut() {
                Some(item) => item.push(block),
                None => items.push(vec![block]),
            },
        }
    }

    fn close_container(&mut self) {
        let Some(container) = self.containers.pop() else {
            return;
        };
        match container {
            Container::Item(blocks) => match self.containers.last_mut() {
                Some(Container::List { items, .. }) => items.push(blocks),
                _ => {
                    for block in blocks {
                        self.push_block(block);
                    }
                }
            },
            Container::Quote(blocks) => self.push_block(Block::Quote { blocks }),
            Container::List {
                ordered,
                start,
                items,
            } => self.push_block(Block::List {
                ordered,
                start,
                items,
            }),
        }
    }

    fn open_inline_leaf(&mut self, owner: InlineOwner) {
        self.close_implicit();
        self.inline = Some(InlineLeaf::new(owner));
    }

    /// The open paragraph or heading; inline content outside one (a tight list item) opens an implicit paragraph.
    fn inline_leaf(&mut self) -> &mut InlineLeaf {
        self.inline
            .get_or_insert_with(|| InlineLeaf::new(InlineOwner::Paragraph { implicit: true }))
    }

    /// Closes a paragraph opened by inline content outside one, at the next block event (FR-052's tight-list rule).
    fn close_implicit(&mut self) {
        if let Some(InlineLeaf {
            owner: InlineOwner::Paragraph { implicit: true },
            ..
        }) = self.inline
        {
            self.close_inline_leaf();
        }
    }

    fn close_inline_leaf(&mut self) {
        if let Some(leaf) = self.inline.take() {
            self.push_block(leaf.into_block());
        }
    }

    fn close_raw(&mut self) {
        let block = match self.raw.take() {
            None => return,
            Some(RawLeaf::Code(text)) => Block::Code { text },
            Some(RawLeaf::Html(mut text)) => {
                // PD-9: the final line break is removed.
                if text.ends_with('\n') {
                    text.pop();
                    if text.ends_with('\r') {
                        text.pop();
                    }
                }
                Block::Paragraph {
                    inlines: vec![Inline::Text { text }],
                }
            }
        };
        self.push_block(block);
    }

    fn text(&mut self, s: &str) {
        match &mut self.raw {
            Some(RawLeaf::Code(buf) | RawLeaf::Html(buf)) => buf.push_str(s),
            None => self.inline_leaf().push(Inline::Text { text: s.to_owned() }),
        }
    }
}

#[cfg(test)]
#[path = "notes_tests.rs"]
mod tests;
