package dashboard

import (
	"fmt"
	"os/exec"
	"runtime"
	"strings"
)

var activationExecCommand = exec.Command

func runActivationHook(cmd *exec.Cmd) {
	if runtime.GOOS != "darwin" || cmd == nil || cmd.Process == nil {
		return
	}

	pid := cmd.Process.Pid
	if pid <= 0 {
		return
	}

	script := macOSFrontmostScript(pid)
	activateCmd := activationExecCommand("osascript", "-e", script)
	activateCmd.Stdout = nil
	activateCmd.Stderr = nil
	_ = activateCmd.Run()
}

func macOSFrontmostScript(pid int) string {
	var script strings.Builder
	script.WriteString("repeat 60 times\n")
	script.WriteString("tell application \"System Events\"\n")
	script.WriteString("try\n")
	script.WriteString(fmt.Sprintf("set frontmost of (first process whose unix id is %d) to true\n", pid))
	script.WriteString("return\n")
	script.WriteString("end try\n")
	script.WriteString("end tell\n")
	script.WriteString("delay 0.1\n")
	script.WriteString("end repeat")
	return script.String()
}
