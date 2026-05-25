package llm

import "testing"

func TestParseStreamDeltaOpenAIChunk(t *testing.T) {
	delta, err := parseStreamDelta(`{"choices":[{"delta":{"content":"hello"}}]}`)
	if err != nil {
		t.Fatal(err)
	}
	if delta.Text != "hello" {
		t.Fatalf("text = %q, want hello", delta.Text)
	}
}

func TestParseStreamDeltaLlamaContentChunk(t *testing.T) {
	delta, err := parseStreamDelta(`{"content":"world"}`)
	if err != nil {
		t.Fatal(err)
	}
	if delta.Text != "world" {
		t.Fatalf("text = %q, want world", delta.Text)
	}
}

func TestParseStreamDeltaReasoningChunk(t *testing.T) {
	delta, err := parseStreamDelta(`{"choices":[{"delta":{"reasoning_content":"thinking"}}]}`)
	if err != nil {
		t.Fatal(err)
	}
	if delta.Reasoning != "thinking" {
		t.Fatalf("reasoning = %q, want thinking", delta.Reasoning)
	}
}

func TestParseStreamDeltaFinishReason(t *testing.T) {
	delta, err := parseStreamDelta(`{"choices":[{"finish_reason":"length","delta":{}}]}`)
	if err != nil {
		t.Fatal(err)
	}
	if delta.FinishReason != "length" {
		t.Fatalf("finish reason = %q, want length", delta.FinishReason)
	}
}
