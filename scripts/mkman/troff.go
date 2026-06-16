package main

import (
	"fmt"
	"io"
	"strings"

	"aruu/mkman/djot"
)

type TroffRenderer struct {
	doc       *djot.Document
	w         io.Writer
	section   int
	date      string
	thDone    bool
	listDepth int
}

func NewTroffRenderer(doc *djot.Document, w io.Writer, section int, date string) *TroffRenderer {
	return &TroffRenderer{doc: doc, w: w, section: section, date: date}
}

func (r *TroffRenderer) getBytes(start, end int32) []byte {
	if start == -1 && end == -1 {
		return nil
	}
	if start < 0 {
		idx := ^start
		if idx < 0 || idx >= int32(len(r.doc.Extra)) {
			return nil
		}
		return r.doc.Extra[idx : end+1]
	}
	if start >= int32(len(r.doc.Source)) || end >= int32(len(r.doc.Source)) || start > end {
		return nil
	}
	return r.doc.Source[start : end+1]
}

func troffEscape(s string) string {
	s = strings.ReplaceAll(s, "\\", "\\\\")
	s = strings.ReplaceAll(s, "-", "\\-")
	// Leading dot is escaped to prevent troff from treating it as a macro
	if len(s) > 0 && s[0] == '.' {
		s = "\\&" + s
	}
	return s
}

func (r *TroffRenderer) collectInlineText(idx int32) string {
	var b strings.Builder
	curr := r.doc.Nodes[idx].Child
	for curr != -1 {
		node := r.doc.Nodes[curr]
		switch node.Type {
		case djot.NodeStr:
			b.Write(r.getBytes(node.Start, node.End))
		case djot.NodeSoftBreak:
			b.WriteRune(' ')
		case djot.NodeVerbatim:
			b.Write(r.getBytes(node.Start, node.End))
		default:
			b.WriteString(r.collectInlineText(curr))
		}
		curr = node.Next
	}
	return b.String()
}

func (r *TroffRenderer) renderInlines(idx int32) {
	curr := r.doc.Nodes[idx].Child
	for curr != -1 {
		r.renderInlineNode(curr)
		curr = r.doc.Nodes[curr].Next
	}
}

func (r *TroffRenderer) renderInlineNode(idx int32) {
	node := r.doc.Nodes[idx]
	switch node.Type {
	case djot.NodeStr:
		raw := string(r.getBytes(node.Start, node.End))
		fmt.Fprint(r.w, troffEscape(raw))
	case djot.NodeSoftBreak:
		fmt.Fprint(r.w, " ")
	case djot.NodeHardBreak:
		fmt.Fprint(r.w, "\n.br\n")
	case djot.NodeNonBreakingSpace:
		fmt.Fprint(r.w, "\\ ")
	case djot.NodeEmph:
		fmt.Fprint(r.w, `\fI`)
		r.renderInlines(idx)
		fmt.Fprint(r.w, `\fP`)
	case djot.NodeStrong:
		fmt.Fprint(r.w, `\fB`)
		r.renderInlines(idx)
		fmt.Fprint(r.w, `\fP`)
	case djot.NodeVerbatim:
		fmt.Fprint(r.w, `\fB`)
		raw := string(r.getBytes(node.Start, node.End))
		fmt.Fprint(r.w, troffEscape(raw))
		fmt.Fprint(r.w, `\fP`)
	case djot.NodeSmartPunctuation:
		data := node.Data
		switch data {
		case 1:
			fmt.Fprint(r.w, `\(em`)
		case 2:
			fmt.Fprint(r.w, `\(en`)
		default:
			fmt.Fprint(r.w, `\-\-`)
		}
	case djot.NodeLink:
		// Render link text only because raw urls are typically omitted in man pages
		r.renderInlines(idx)
	case djot.NodeDoubleQuoted:
		fmt.Fprint(r.w, `\(lq`)
		r.renderInlines(idx)
		fmt.Fprint(r.w, `\(rq`)
	case djot.NodeSingleQuoted:
		fmt.Fprint(r.w, `\(oq`)
		r.renderInlines(idx)
		fmt.Fprint(r.w, `\(cq`)
	default:
		r.renderInlines(idx)
	}
}

func (r *TroffRenderer) renderChildren(idx int32) {
	curr := r.doc.Nodes[idx].Child
	for curr != -1 {
		r.renderNode(curr)
		curr = r.doc.Nodes[curr].Next
	}
}

func (r *TroffRenderer) renderNode(idx int32) {
	node := r.doc.Nodes[idx]
	switch node.Type {
	case djot.NodeDoc:
		r.renderChildren(idx)

	case djot.NodeSection:
		r.renderChildren(idx)

	case djot.NodeHeading:
		level := int(node.Level)
		if level == 0 {
			level = int(node.Data & 0xFFFF)
		}
		text := r.collectInlineText(idx)
		switch level {
		case 1:
			if !r.thDone {
				fmt.Fprintf(r.w, ".TH %s %d \"%s\"\n",
					strings.ToUpper(text), r.section, r.date)
				r.thDone = true
			} else {
				fmt.Fprintf(r.w, ".SH %s\n", strings.ToUpper(text))
			}
		case 2:
			fmt.Fprintf(r.w, ".SH %s\n", strings.ToUpper(text))
		case 3:
			fmt.Fprintf(r.w, ".SS %s\n", text)
		default:
			fmt.Fprintf(r.w, ".PP\n\\fB%s\\fP\n", troffEscape(text))
		}

	case djot.NodePara:
		if r.listDepth > 0 {
			r.renderInlines(idx)
			fmt.Fprint(r.w, "\n")
		} else {
			fmt.Fprint(r.w, ".PP\n")
			r.renderInlines(idx)
			fmt.Fprint(r.w, "\n")
		}

	case djot.NodeCodeBlock:
		fmt.Fprint(r.w, ".nf\n")
		if node.Child != -1 {
			r.renderChildren(idx)
		} else {
			raw := r.getBytes(node.Start, node.End)
			r.w.Write(raw) //nolint
		}
		fmt.Fprint(r.w, ".fi\n")

	case djot.NodeStr:
		raw := string(r.getBytes(node.Start, node.End))
		fmt.Fprint(r.w, raw)

	case djot.NodeBlockQuote:
		fmt.Fprint(r.w, ".RS\n")
		r.renderChildren(idx)
		fmt.Fprint(r.w, ".RE\n")

	case djot.NodeBulletList, djot.NodeOrderedList, djot.NodeTaskList:
		r.listDepth++
		r.renderChildren(idx)
		r.listDepth--

	case djot.NodeListItem, djot.NodeTaskListItem:
		if r.doc.Nodes[idx].Type == djot.NodeTaskListItem {
			checked := (node.Data & djot.DataTaskChecked) != 0
			if checked {
				fmt.Fprint(r.w, ".IP \"[x]\" 4\n")
			} else {
				fmt.Fprint(r.w, ".IP \"[ ]\" 4\n")
			}
		} else {
			fmt.Fprint(r.w, ".IP \\(bu 4\n")
		}
		r.renderChildren(idx)

	case djot.NodeDefinitionList:
		r.renderChildren(idx)

	case djot.NodeDefinitionListItem:
		r.renderChildren(idx)

	case djot.NodeTerm:
		fmt.Fprint(r.w, ".TP\n")
		r.renderInlines(idx)
		fmt.Fprint(r.w, "\n")

	case djot.NodeDefinition:
		r.listDepth++
		r.renderChildren(idx)
		r.listDepth--

	case djot.NodeThematicBreak:
		// Omit thematic breaks because troff lacks a clean representation

	case djot.NodeDiv:
		r.renderChildren(idx)

	default:
	}
}

func (r *TroffRenderer) Render() {
	if len(r.doc.Nodes) == 0 {
		return
	}
	r.renderNode(0)
}
