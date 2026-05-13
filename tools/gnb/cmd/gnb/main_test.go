package main

import (
	"reflect"
	"testing"

	"github.com/spf13/cobra"
)

func TestResolveIOSSkipCodeSignDefault(t *testing.T) {
	cmd := &cobra.Command{Use: "ios"}
	cmd.Flags().Bool("skip-codesign", true, "")
	cmd.Flags().Bool("codesign", false, "")

	got, err := resolveIOSSkipCodeSign(cmd, true, false)
	if err != nil {
		t.Fatalf("resolveIOSSkipCodeSign returned error: %v", err)
	}
	if !got {
		t.Fatalf("resolveIOSSkipCodeSign() = %v, want true", got)
	}
}

func TestResolveIOSSkipCodeSignWithCodesignFlag(t *testing.T) {
	cmd := &cobra.Command{Use: "ios"}
	skipCodeSign := true
	codeSign := false
	cmd.Flags().BoolVar(&skipCodeSign, "skip-codesign", true, "")
	cmd.Flags().BoolVar(&codeSign, "codesign", false, "")
	if err := cmd.ParseFlags([]string{"--codesign"}); err != nil {
		t.Fatalf("ParseFlags returned error: %v", err)
	}

	got, err := resolveIOSSkipCodeSign(cmd, skipCodeSign, codeSign)
	if err != nil {
		t.Fatalf("resolveIOSSkipCodeSign returned error: %v", err)
	}
	if got {
		t.Fatalf("resolveIOSSkipCodeSign() = %v, want false", got)
	}
}

func TestResolveIOSSkipCodeSignRejectsConflictingFlags(t *testing.T) {
	cmd := &cobra.Command{Use: "ios"}
	skipCodeSign := true
	codeSign := false
	cmd.Flags().BoolVar(&skipCodeSign, "skip-codesign", true, "")
	cmd.Flags().BoolVar(&codeSign, "codesign", false, "")
	if err := cmd.ParseFlags([]string{"--skip-codesign", "--codesign"}); err != nil {
		t.Fatalf("ParseFlags returned error: %v", err)
	}

	if _, err := resolveIOSSkipCodeSign(cmd, skipCodeSign, codeSign); err == nil {
		t.Fatal("resolveIOSSkipCodeSign() error = nil, want conflict error")
	}
}

func TestParseRunArgsPassesTargetArgsWithoutSeparator(t *testing.T) {
	opts, showHelp, err := parseRunArgs("windows", []string{"gkNextRenderer", "--help"})
	if err != nil {
		t.Fatalf("parseRunArgs returned error: %v", err)
	}
	if showHelp {
		t.Fatal("parseRunArgs showHelp = true, want false")
	}
	if opts.Target != "gkNextRenderer" {
		t.Fatalf("Target = %q, want gkNextRenderer", opts.Target)
	}
	if !reflect.DeepEqual(opts.Args, []string{"--help"}) {
		t.Fatalf("Args = %#v, want --help", opts.Args)
	}
}

func TestParseRunArgsKeepsRunFlagsBeforeTarget(t *testing.T) {
	opts, showHelp, err := parseRunArgs("windows", []string{"--dry-run", "--scene", "Demo.proc", "--present-mode=mailbox", "gkNextRenderer", "--help"})
	if err != nil {
		t.Fatalf("parseRunArgs returned error: %v", err)
	}
	if showHelp {
		t.Fatal("parseRunArgs showHelp = true, want false")
	}
	if !opts.DryRun {
		t.Fatal("DryRun = false, want true")
	}
	if opts.Target != "gkNextRenderer" {
		t.Fatalf("Target = %q, want gkNextRenderer", opts.Target)
	}
	if !reflect.DeepEqual(opts.Scenes, []string{"Demo.proc"}) {
		t.Fatalf("Scenes = %#v, want Demo.proc", opts.Scenes)
	}
	if !reflect.DeepEqual(opts.PresentModes, []string{"mailbox"}) {
		t.Fatalf("PresentModes = %#v, want mailbox", opts.PresentModes)
	}
	if !reflect.DeepEqual(opts.Args, []string{"--help"}) {
		t.Fatalf("Args = %#v, want --help", opts.Args)
	}
}

func TestParseRunArgsShowsRunHelpWithoutTarget(t *testing.T) {
	_, showHelp, err := parseRunArgs("windows", []string{"--help"})
	if err != nil {
		t.Fatalf("parseRunArgs returned error: %v", err)
	}
	if !showHelp {
		t.Fatal("parseRunArgs showHelp = false, want true")
	}
}
