package scadgen

import (
	"context"
	"encoding/json"
	"fmt"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/scadcompose"
)

// ChatFn sends one chat round (full message history) and returns the reply.
// Wraps llm.Client in production; mocked in tests.
type ChatFn func(ctx context.Context, messages []Message) (string, error)

// Message mirrors the OpenAI role/content pair without importing internal/llm
// (keeps this package free of server concerns and easily testable).
type Message struct {
	Role    string
	Content string
}

// Options tunes the generation loop.
type Options struct {
	MaxRepairs int // extra rounds after the first reply (default 2)
}

// Outcome is a successful generation.
type Outcome struct {
	Spec       *scadcompose.Spec
	SpecJSON   []byte // pretty-printed, as written to specs/
	Source     string // composed .scad
	Warnings   []string
	Rounds     int // chat rounds used (1 = first shot)
	Transcript []Message
}

// ExtractJSON pulls the first JSON object out of an LLM reply, tolerating
// markdown fences and prose around it.
func ExtractJSON(reply string) (string, error) {
	text := strings.TrimSpace(reply)
	if fence := strings.Index(text, "```"); fence >= 0 {
		rest := text[fence+3:]
		if newline := strings.IndexByte(rest, '\n'); newline >= 0 {
			rest = rest[newline+1:] // drop the ```json language tag line
		}
		if end := strings.Index(rest, "```"); end >= 0 {
			text = rest[:end]
		} else {
			text = rest
		}
		text = strings.TrimSpace(text)
	}
	start := strings.IndexByte(text, '{')
	end := strings.LastIndexByte(text, '}')
	if start < 0 || end <= start {
		return "", fmt.Errorf("no JSON object in reply")
	}
	return text[start : end+1], nil
}

// Generate runs the request -> spec -> validate loop. On validation failure
// the error is fed back to the model, up to opts.MaxRepairs extra rounds.
func Generate(ctx context.Context, chat ChatFn, catalog *scadcompose.Catalog, menu string,
	request string, opts Options) (*Outcome, error) {
	maxRepairs := opts.MaxRepairs
	if maxRepairs <= 0 {
		maxRepairs = 2
	}
	messages := []Message{
		{Role: "system", Content: SystemPrompt},
		{Role: "user", Content: BuildUserPrompt(request, menu)},
	}

	var lastProblem string
	for round := 1; round <= 1+maxRepairs; round++ {
		reply, err := chat(ctx, messages)
		if err != nil {
			return nil, &GenerateError{Transcript: messages, Err: err}
		}
		messages = append(messages, Message{Role: "assistant", Content: reply})

		outcome, problem := tryValidate(catalog, reply)
		if problem == "" {
			outcome.Rounds = round
			outcome.Transcript = messages
			return outcome, nil
		}
		lastProblem = problem
		messages = append(messages, Message{Role: "user", Content: BuildRepairPrompt(problem)})
	}
	return nil, &GenerateError{
		Transcript: messages,
		Err:        fmt.Errorf("spec still invalid after %d rounds: %s", 1+maxRepairs, lastProblem),
	}
}

// GenerateError carries the chat transcript for debugging failed runs.
type GenerateError struct {
	Transcript []Message
	Err        error
}

func (e *GenerateError) Error() string { return e.Err.Error() }
func (e *GenerateError) Unwrap() error { return e.Err }

// tryValidate parses + composes one reply; returns the outcome or a problem
// description suitable for feeding back to the model.
func tryValidate(catalog *scadcompose.Catalog, reply string) (*Outcome, string) {
	jsonText, err := ExtractJSON(reply)
	if err != nil {
		return nil, err.Error()
	}
	spec, err := scadcompose.ParseSpec([]byte(jsonText), "spec")
	if err != nil {
		return nil, err.Error()
	}
	result, err := scadcompose.Compose(spec, catalog, "specs/"+spec.Name+".json", "llm")
	if err != nil {
		return nil, err.Error()
	}
	pretty, err := json.MarshalIndent(json.RawMessage(jsonText), "", "  ")
	if err != nil {
		pretty = []byte(jsonText)
	}
	return &Outcome{Spec: spec, SpecJSON: append(pretty, '\n'), Source: result.Source, Warnings: result.Warnings}, ""
}
