package dashboard

import (
	"os/exec"
	"strings"
	"testing"
)

func TestMacOSFrontmostScriptContainsPIDAndRetryLoop(t *testing.T) {
	script := macOSFrontmostScript(4242)
	if !strings.Contains(script, "repeat 60 times") {
		t.Fatalf("script missing retry loop:\n%s", script)
	}
	if !strings.Contains(script, "unix id is 4242") {
		t.Fatalf("script missing pid:\n%s", script)
	}
	if !strings.Contains(script, "delay 0.1") {
		t.Fatalf("script missing retry delay:\n%s", script)
	}
}

func TestRunActivationHookNilWithoutStartedProcess(t *testing.T) {
	runActivationHook(nil)

	cmd := &exec.Cmd{}
	runActivationHook(cmd)
}
