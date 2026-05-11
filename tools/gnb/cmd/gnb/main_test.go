package main

import (
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
