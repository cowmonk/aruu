package main

type Page struct {
	Name        string
	Summary     string
	Section     int
	Date        string
	Synopsis    []SynopsisForm
	Description []Block
	Options     []Option
	Sections    []Section
}

type Section struct {
	Title  string
	Blocks []Block
}

type BlockKind int

const (
	BlockParagraph BlockKind = iota
	BlockTaggedList
	BlockSeeAlso
	BlockSubsection
)

type Block struct {
	Kind    BlockKind
	Inlines []Inline
	Items   []Item
	Refs    []XRef
	Title   string
	Blocks  []Block
}

type Item struct {
	Label []Inline
	Body  []Inline
}

type XRef struct {
	Name    string
	Section string
}

type Option struct {
	Spec []SynopsisItem
	Desc string
	Body []Block
	Key  string
}

type SynopsisForm struct {
	Items []SynopsisItem
}

type SynopsisItemKind int

const (
	SynFlag SynopsisItemKind = iota
	SynArg
	SynCommand
	SynLiteral
	SynOptional
	SynPipe
)

type SynopsisItem struct {
	Kind     SynopsisItemKind
	Text     string
	Children []SynopsisItem
}

type InlineKind int

const (
	InlineText InlineKind = iota
	InlineNm
	InlineEmph
	InlineLiteral
	InlinePath
	InlineXRef
	InlineFlag
)

type Inline struct {
	Kind     InlineKind
	Text     string
	Section  string
	Children []Inline
}
