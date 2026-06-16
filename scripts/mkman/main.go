package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"

	"aruu/mkman/djot"
)

// Parse config.mk variables and values
//
type Config map[string]string

func parseConfigMk(path string) (Config, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	raw := make(Config)
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		eq := strings.IndexByte(line, '=')
		if eq < 0 {
			continue
		}
		k := strings.TrimSpace(line[:eq])
		v := strings.TrimSpace(line[eq+1:])
		raw[k] = v
	}
	if err := sc.Err(); err != nil {
		return nil, err
	}

	cfg := make(Config, len(raw))
	for k, v := range raw {
		cfg[k] = expandVars(v, raw)
	}
	return cfg, nil
}

func expandVars(s string, env Config) string {
	for {
		start := strings.Index(s, "$(")
		if start < 0 {
			break
		}
		end := strings.Index(s[start:], ")")
		if end < 0 {
			break
		}
		end += start
		varname := s[start+2 : end]
		replacement := ""
		if v, ok := env[varname]; ok {
			replacement = v
		}
		s = s[:start] + replacement + s[end+1:]
	}
	return s
}

func (cfg Config) isEnabled(key string) bool {
	return cfg[key] == "1"
}

// Preprocessor condition stack for nested feature blocks
//
type ifFrame struct {
	active bool
	seen   bool
}

type ifStack struct {
	frames []ifFrame
}

func (s *ifStack) push(active, seen bool) {
	s.frames = append(s.frames, ifFrame{active: active, seen: seen})
}

func (s *ifStack) pop() {
	if len(s.frames) > 0 {
		s.frames = s.frames[:len(s.frames)-1]
	}
}

func (s *ifStack) parentActive() bool {
	for i := 0; i < len(s.frames)-1; i++ {
		if !s.frames[i].active {
			return false
		}
	}
	return true
}

func (s *ifStack) globallyActive() bool {
	for _, f := range s.frames {
		if !f.active {
			return false
		}
	}
	return true
}

func (s *ifStack) top() *ifFrame {
	if len(s.frames) == 0 {
		return nil
	}
	return &s.frames[len(s.frames)-1]
}

// Parse and evaluate preprocessor feature conditions
//
func evalCondition(expr string, cfg Config) bool {
	expr = strings.TrimSpace(expr)
	if strings.HasPrefix(expr, "defined(") && strings.HasSuffix(expr, ")") {
		key := expr[8 : len(expr)-1]
		_, ok := cfg[key]
		return ok
	}
	if strings.HasPrefix(expr, "!defined(") && strings.HasSuffix(expr, ")") {
		key := expr[9 : len(expr)-1]
		_, ok := cfg[key]
		return !ok
	}
	if idx := strings.Index(expr, "=="); idx >= 0 {
		lhs := strings.TrimSpace(expr[:idx])
		rhs := strings.TrimSpace(expr[idx+2:])
		return cfg[lhs] == rhs
	}
	if idx := strings.Index(expr, "!="); idx >= 0 {
		lhs := strings.TrimSpace(expr[:idx])
		rhs := strings.TrimSpace(expr[idx+2:])
		return cfg[lhs] != rhs
	}
	if strings.HasPrefix(expr, "!") {
		key := strings.TrimSpace(expr[1:])
		return cfg[key] != "1"
	}
	return cfg[expr] == "1"
}

// Scanning and parsing djot comments in c source files
//
func formatToken(tok string) string {
	if strings.HasPrefix(tok, "-") {
		return "`" + tok + "`"
	}
	if len(tok) >= 2 {
		first := tok[0]
		last := tok[len(tok)-1]
		if (first == '[' && last == ']') || (first == '<' && last == '>') || (first == '(' && last == ')') {
			inner := tok[1 : len(tok)-1]
			if strings.HasPrefix(inner, "-") {
				return string(first) + "`" + inner + "`" + string(last)
			}
			return string(first) + "_" + inner + "_" + string(last)
		}
	}
	return "_" + tok + "_"
}

func parseNameSummary(line string) (string, string, bool) {
	line = strings.TrimSpace(line)
	if idx := strings.Index(line, ":"); idx >= 0 {
		return strings.TrimSpace(line[:idx]), strings.TrimSpace(line[idx+1:]), true
	}
	if idx := strings.Index(line, " \\- "); idx >= 0 {
		return strings.TrimSpace(line[:idx]), strings.TrimSpace(line[idx+4:]), true
	}
	if idx := strings.Index(line, " - "); idx >= 0 {
		return strings.TrimSpace(line[:idx]), strings.TrimSpace(line[idx+3:]), true
	}
	return "", "", false
}

func isOptionPattern(s string) (string, string, bool) {
	s = strings.TrimSpace(s)
	if !strings.HasPrefix(s, "-") {
		return "", "", false
	}
	idx := strings.Index(s, ":")
	if idx < 0 {
		return "", "", false
	}
	opt := strings.TrimSpace(s[:idx])
	desc := strings.TrimSpace(s[idx+1:])
	fields := strings.Fields(opt)
	if len(fields) == 0 || !strings.HasPrefix(fields[0], "-") {
		return "", "", false
	}
	return opt, desc, true
}

func scanC(path string, cfg Config) (string, error) {
	contentBytes, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	contentStr := string(contentBytes)

	if !strings.Contains(contentStr, "?man") {
		// Original parser mode using explicit man markers
		var djotBuf strings.Builder
		stack := &ifStack{}
		sc := bufio.NewScanner(strings.NewReader(contentStr))
		sc.Buffer(make([]byte, 1<<20), 1<<20)
		inBlockComment := false

		for sc.Scan() {
			line := sc.Text()
			trimmed := strings.TrimSpace(line)

			if inBlockComment {
				if trimmed == "*/" || trimmed == "* /" {
					inBlockComment = false
					djotBuf.WriteByte('\n')
				} else {
					stripped := trimmed
					if strings.HasPrefix(stripped, "* ") {
						stripped = stripped[2:]
					} else if stripped == "*" {
						stripped = ""
					}
					djotBuf.WriteString(stripped)
					djotBuf.WriteByte('\n')
				}
				continue
			}

			if strings.HasPrefix(trimmed, "#") {
				directive := trimmed[1:]
				if ci := strings.Index(directive, "//"); ci >= 0 {
					directive = directive[:ci]
				}
				directive = strings.TrimSpace(directive)

				switch {
				case strings.HasPrefix(directive, "ifdef "):
					key := strings.TrimSpace(directive[6:])
					_, defined := cfg[key]
					pa := stack.parentActive()
					stack.push(pa && defined, defined)
				case strings.HasPrefix(directive, "ifndef "):
					key := strings.TrimSpace(directive[7:])
					_, defined := cfg[key]
					pa := stack.parentActive()
					stack.push(pa && !defined, !defined)
				case strings.HasPrefix(directive, "if "):
					expr := strings.TrimSpace(directive[3:])
					result := evalCondition(expr, cfg)
					pa := stack.parentActive()
					stack.push(pa && result, result)
				case directive == "else":
					if top := stack.top(); top != nil {
						pa := stack.parentActive()
						newActive := pa && !top.seen
						top.active = newActive
						top.seen = true
					}
				case strings.HasPrefix(directive, "elif "):
					if top := stack.top(); top != nil {
						expr := strings.TrimSpace(directive[5:])
						result := evalCondition(expr, cfg)
						pa := stack.parentActive()
						newActive := pa && !top.seen && result
						top.active = newActive
						if result {
							top.seen = true
						}
					}
				case directive == "endif":
					stack.pop()
				}
				continue
			}

			if !stack.globallyActive() {
				continue
			}

			if strings.Contains(trimmed, "/* !man") {
				startIdx := strings.Index(trimmed, "/* !man")
				rest := strings.TrimSpace(trimmed[startIdx+7:])
				if strings.HasSuffix(rest, "*/") {
					content := strings.TrimSpace(rest[:len(rest)-2])
					if content != "" {
						djotBuf.WriteString(content)
						djotBuf.WriteByte('\n')
					}
				} else {
					if rest != "" {
						djotBuf.WriteString(rest)
						djotBuf.WriteByte('\n')
					}
					inBlockComment = true
				}
				continue
			}

			if idx := strings.Index(trimmed, "// !man"); idx >= 0 {
				content := strings.TrimSpace(trimmed[idx+7:])
				djotBuf.WriteString(content)
				djotBuf.WriteByte('\n')
				continue
			}
		}

		if err := sc.Err(); err != nil {
			return "", err
		}
		return djotBuf.String(), nil
	}

	// Heuristic parser mode using question mark man markers
	type optionDoc struct {
		opt  string
		desc string
	}
	type sectionDoc struct {
		title string
		lines []string
	}

	var name, summary, synopsis string
	var descriptionBody []string
	var options []optionDoc
	var otherSections []sectionDoc
	var currentSection *sectionDoc
	var lastOption *optionDoc

	stack := &ifStack{}
	sc := bufio.NewScanner(strings.NewReader(contentStr))
	sc.Buffer(make([]byte, 1<<20), 1<<20)
	inBlockComment := false
	firstLineOfFirstBlock := true

	for sc.Scan() {
		line := sc.Text()
		trimmed := strings.TrimSpace(line)

		if inBlockComment {
			if trimmed == "*/" || trimmed == "* /" {
				inBlockComment = false
			} else {
				stripped := trimmed
				if strings.HasPrefix(stripped, "* ") {
					stripped = stripped[2:]
				} else if stripped == "*" {
					stripped = ""
				}
				if strings.HasPrefix(stripped, "// ?man ") {
					stripped = stripped[8:]
				} else if strings.HasPrefix(stripped, "// ?man") {
					stripped = stripped[7:]
				}
				if firstLineOfFirstBlock {
					firstLineOfFirstBlock = false
					if n, s, ok := parseNameSummary(stripped); ok {
						name = n
						summary = s
						continue
					}
				}
				if synopsis == "" && strings.HasPrefix(strings.ToLower(stripped), "usage:") {
					synopsis = strings.TrimSpace(stripped[6:])
					continue
				}
				if strings.HasPrefix(stripped, "## ") {
					title := strings.TrimSpace(stripped[3:])
					otherSections = append(otherSections, sectionDoc{title: title})
					currentSection = &otherSections[len(otherSections)-1]
					lastOption = nil
				} else {
					if opt, desc, ok := isOptionPattern(stripped); ok {
						options = append(options, optionDoc{opt: opt, desc: desc})
						lastOption = &options[len(options)-1]
						currentSection = nil
					} else {
						if currentSection != nil {
							currentSection.lines = append(currentSection.lines, stripped)
						} else {
							descriptionBody = append(descriptionBody, stripped)
						}
					}
				}
			}
			continue
		}

		if strings.HasPrefix(trimmed, "#") {
			directive := trimmed[1:]
			if ci := strings.Index(directive, "//"); ci >= 0 {
				directive = directive[:ci]
			}
			directive = strings.TrimSpace(directive)

			switch {
			case strings.HasPrefix(directive, "ifdef "):
				key := strings.TrimSpace(directive[6:])
				_, defined := cfg[key]
				pa := stack.parentActive()
				stack.push(pa && defined, defined)
			case strings.HasPrefix(directive, "ifndef "):
				key := strings.TrimSpace(directive[7:])
				_, defined := cfg[key]
				pa := stack.parentActive()
				stack.push(pa && !defined, !defined)
			case strings.HasPrefix(directive, "if "):
				expr := strings.TrimSpace(directive[3:])
				result := evalCondition(expr, cfg)
				pa := stack.parentActive()
				stack.push(pa && result, result)
			case directive == "else":
				if top := stack.top(); top != nil {
					pa := stack.parentActive()
					newActive := pa && !top.seen
					top.active = newActive
					top.seen = true
				}
			case strings.HasPrefix(directive, "elif "):
				if top := stack.top(); top != nil {
					expr := strings.TrimSpace(directive[5:])
					result := evalCondition(expr, cfg)
					pa := stack.parentActive()
					newActive := pa && !top.seen && result
					top.active = newActive
					if result {
						top.seen = true
					}
				}
			case directive == "endif":
				stack.pop()
			}
			continue
		}

		if !stack.globallyActive() {
			continue
		}

		if strings.Contains(trimmed, "/* ?man") {
			startIdx := strings.Index(trimmed, "/* ?man")
			rest := strings.TrimSpace(trimmed[startIdx+7:])
			if strings.HasSuffix(rest, "*/") {
				content := strings.TrimSpace(rest[:len(rest)-2])
				if n, s, ok := parseNameSummary(content); ok {
					name = n
					summary = s
				}
			} else {
				if rest != "" {
					if n, s, ok := parseNameSummary(rest); ok {
						name = n
						summary = s
					} else {
						descriptionBody = append(descriptionBody, rest)
					}
				}
				inBlockComment = true
			}
			continue
		}

		if idx := strings.Index(trimmed, "// ?man"); idx >= 0 {
			content := strings.TrimSpace(trimmed[idx+7:])
			if opt, desc, ok := isOptionPattern(content); ok {
				options = append(options, optionDoc{opt: opt, desc: desc})
				lastOption = &options[len(options)-1]
				currentSection = nil
			} else {
				if lastOption != nil {
					lastOption.desc += " " + content
				} else if currentSection != nil {
					currentSection.lines = append(currentSection.lines, content)
				} else {
					descriptionBody = append(descriptionBody, content)
				}
			}
			continue
		}
	}

	if err := sc.Err(); err != nil {
		return "", err
	}

	var djotBuf strings.Builder
	djotBuf.WriteString("# " + name + "\n\n")
	djotBuf.WriteString("## NAME\n\n")
	djotBuf.WriteString(name + " \\- " + summary + "\n\n")

	if synopsis != "" {
		djotBuf.WriteString("## SYNOPSIS\n\n")
		djotBuf.WriteString("```\n" + synopsis + "\n```\n\n")
	}

	if len(descriptionBody) > 0 {
		djotBuf.WriteString("## DESCRIPTION\n\n")
		for _, l := range descriptionBody {
			djotBuf.WriteString(l + "\n")
		}
		djotBuf.WriteString("\n")
	}

	if len(options) > 0 {
		djotBuf.WriteString("## OPTIONS\n\n")
		for _, opt := range options {
			words := strings.Fields(opt.opt)
			var formatted []string
			for _, w := range words {
				formatted = append(formatted, formatToken(w))
			}
			djotBuf.WriteString(strings.Join(formatted, " ") + "\n")
			djotBuf.WriteString(": " + opt.desc + "\n\n")
		}
	}

	for _, sec := range otherSections {
		djotBuf.WriteString("## " + sec.title + "\n\n")
		for _, l := range sec.lines {
			djotBuf.WriteString(l + "\n")
		}
		djotBuf.WriteString("\n")
	}

	return djotBuf.String(), nil
}

// Section inference from file path
//
func inferSection(path string) int {
	clean := filepath.ToSlash(path)
	switch {
	case strings.Contains(clean, "/linux/"),
		strings.Contains(clean, "/net/"),
		strings.Contains(clean, "/xsi/"):
		return 8
	default:
		return 1
	}
}

// Main entry point and flags definition
//
func main() {
	configPath := flag.String("config", "config.mk", "path to config.mk")
	sectionFlag := flag.Int("section", 0, "man section override (0 = infer from path)")
	dateFlag := flag.String("date", "", "date string for TH line (default: current month/year)")
	flag.Parse()

	if flag.NArg() < 1 {
		fmt.Fprintf(os.Stderr, "usage: mkman [-config config.mk] [-section N] file.c\n")
		os.Exit(1)
	}
	cfile := flag.Arg(0)

	cfg, err := parseConfigMk(*configPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "mkman: config: %v\n", err)
		os.Exit(1)
	}

	section := *sectionFlag
	if section == 0 {
		section = inferSection(cfile)
	}

	date := *dateFlag
	if date == "" {
		date = time.Now().Format("January 2006")
	}

	djotText, err := scanC(cfile, cfg)
	if err != nil {
		fmt.Fprintf(os.Stderr, "mkman: scan: %v\n", err)
		os.Exit(1)
	}

	if strings.TrimSpace(djotText) == "" {
		fmt.Fprintf(os.Stderr, "mkman: %s: no !man comments found\n", cfile)
		os.Exit(0)
	}

	doc := djot.Parse([]byte(djotText))

	r := NewTroffRenderer(doc, os.Stdout, section, date)
	r.Render()
}
