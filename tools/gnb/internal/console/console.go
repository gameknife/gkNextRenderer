package console

import (
	"fmt"
	"os"
	"runtime"
	"strings"
)

const (
	reset  = "\x1b[0m"
	bold   = "\x1b[1m"
	cyan   = "\x1b[36m"
	green  = "\x1b[32m"
	yellow = "\x1b[33m"
	red    = "\x1b[31m"
	dim    = "\x1b[2m"
)

func supportsColor() bool {
	if os.Getenv("NO_COLOR") != "" {
		return false
	}
	if runtime.GOOS == "windows" {
		return true
	}
	return os.Getenv("TERM") != "dumb"
}

func color(code string, text string) string {
	if !supportsColor() {
		return text
	}
	return code + text + reset
}

func Header(text string) {
	fmt.Println(color(bold+cyan, text))
}

func Label(name string, value string) {
	fmt.Printf("%s %s\n", color(cyan, fmt.Sprintf("%-10s", name+":")), value)
}

func Info(format string, args ...any) {
	fmt.Printf("%s %s\n", color(cyan, "[gnb]"), fmt.Sprintf(format, args...))
}

func Success(format string, args ...any) {
	fmt.Printf("%s %s\n", color(green, "[ok]"), fmt.Sprintf(format, args...))
}

func Warn(format string, args ...any) {
	fmt.Printf("%s %s\n", color(yellow, "[warn]"), fmt.Sprintf(format, args...))
}

func Error(format string, args ...any) {
	fmt.Fprintf(os.Stderr, "%s %s\n", color(red, "[error]"), fmt.Sprintf(format, args...))
}

func Command(name string, args ...string) {
	cmd := strings.TrimSpace(name + " " + strings.Join(args, " "))
	fmt.Printf("%s %s\n", color(cyan, "[gnb]"), color(bold, cmd))
}

func CommandLine(line string) {
	fmt.Printf("%s %s\n", color(cyan, "[gnb]"), color(bold, line))
}

func Muted(format string, args ...any) {
	fmt.Println(color(dim, fmt.Sprintf(format, args...)))
}

// Bold returns the text wrapped with ANSI bold codes when the terminal supports color.
func Bold(text string) string {
	return color(bold, text)
}

// Dim returns the text wrapped with ANSI dim codes when the terminal supports color.
func Dim(text string) string {
	return color(dim, text)
}
