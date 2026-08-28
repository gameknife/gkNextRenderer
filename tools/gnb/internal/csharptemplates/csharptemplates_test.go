package csharptemplates

import (
	"os"
	"path/filepath"
	"testing"
)

// repoRoot walks up from the package directory to the repository root, which is where the shipped
// templates live.
func repoRoot(t *testing.T) string {
	t.Helper()
	directory, err := os.Getwd()
	if err != nil {
		t.Fatalf("getwd: %v", err)
	}
	for i := 0; i < 8; i++ {
		if _, err := os.Stat(filepath.Join(directory, "AGENTS.md")); err == nil {
			return directory
		}
		parent := filepath.Dir(directory)
		if parent == directory {
			break
		}
		directory = parent
	}
	t.Fatal("could not find the repository root")
	return ""
}

func TestDiscoverFindsShippedTemplates(t *testing.T) {
	ids, err := Discover(repoRoot(t))
	if err != nil {
		t.Fatalf("Discover: %v", err)
	}

	// The five that ship. A new template is welcome; losing one of these is a regression, because
	// the New Game Project dialog offers exactly what this returns.
	for _, want := range []string{"arcade2d", "blank", "firstperson", "topdown3d", "tps"} {
		found := false
		for _, id := range ids {
			if id == want {
				found = true
				break
			}
		}
		if !found {
			t.Errorf("template %q missing from %v", want, ids)
		}
	}
}

// The substitution has to match ManagedGameTemplate.cpp exactly. A template file is named
// __ProjectName__.csproj precisely because {{ }} does not survive being a filename, so both
// spellings have to mean the same thing.
func TestSubstituteHandlesBothSpellings(t *testing.T) {
	tokens := [][2]string{
		{"ProjectName", "MyGame"},
		{"Namespace", "MyGame"},
	}

	cases := []struct{ in, want string }{
		{"__ProjectName__.csproj", "MyGame.csproj"},
		{"namespace {{Namespace}};", "namespace MyGame;"},
		{"class {{ProjectName}}Game", "class MyGameGame"},
		{"{{ProjectName}} and {{ProjectName}}", "MyGame and MyGame"},
		{"nothing to replace", "nothing to replace"},
	}
	for _, testCase := range cases {
		if got := substitute(testCase.in, tokens); got != testCase.want {
			t.Errorf("substitute(%q) = %q, want %q", testCase.in, got, testCase.want)
		}
	}
}

// The project name becomes a namespace and a type name, so whatever a directory is called it has
// to come out a legal C# identifier.
func TestSanitizeIdentifier(t *testing.T) {
	cases := map[string]string{
		"blank":       "blank",
		"top-down-3d": "top_down_3d",
		"first person": "first_person",
	}
	for in, want := range cases {
		if got := sanitizeIdentifier(in); got != want {
			t.Errorf("sanitizeIdentifier(%q) = %q, want %q", in, got, want)
		}
	}
}
