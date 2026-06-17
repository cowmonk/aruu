package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// Parse config.mk variables and values
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

// Section inference from file path
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
		date = time.Now().Format("January 2, 2006")
	}

	page, err := ParsePage(cfile, cfg, section, date)
	if err != nil {
		fmt.Fprintf(os.Stderr, "mkman: scan: %v\n", err)
		os.Exit(1)
	}

	if page == nil {
		fmt.Fprintf(os.Stderr, "mkman: %s: no ?man comments found\n", cfile)
		os.Exit(0)
	}

	r := NewMdocRenderer(page, os.Stdout)
	if err := r.Render(); err != nil {
		fmt.Fprintf(os.Stderr, "mkman: render: %v\n", err)
		os.Exit(1)
	}
}
