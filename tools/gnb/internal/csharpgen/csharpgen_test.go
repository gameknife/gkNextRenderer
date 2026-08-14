package csharpgen

import (
	"strings"
	"testing"
)

func TestParseExtractsEntriesInOrder(t *testing.T) {
	entries, err := Parse(`
// a comment that is not an entry
GK_API(Log, Info, void, (GkStr message))
GK_API(UI, Begin, GkBool, (GkStr name, int32_t flags))   //= flags:0
GK_API(UI, GetScreenSize, void, (FVec2* outSize))
`)
	if err != nil {
		t.Fatalf("parse: %v", err)
	}
	if len(entries) != 3 {
		t.Fatalf("want 3 entries, got %d", len(entries))
	}
	// Order is the field order of FEngineApi; reordering would silently misalign the two sides.
	if entries[0].Symbol() != "Log_Info" || entries[2].Symbol() != "UI_GetScreenSize" {
		t.Fatalf("entries out of order: %v", entries)
	}
	if entries[1].Defaults["flags"] != "0" {
		t.Fatalf("default not parsed: %#v", entries[1].Defaults)
	}
}

func TestParseRejectsBoolAcrossTheBoundary(t *testing.T) {
	// C++ bool has implementation-defined size and C# bool is not blittable, so the def file must
	// use GkBool. Catching it here keeps the failure at generation time instead of at AOT time.
	if _, err := Parse("GK_API(Input, IsKeyDown, bool, (GkStr name))\n"); err == nil {
		t.Fatal("expected bool return to be rejected")
	}
	if _, err := Parse("GK_API(Input, Set, void, (bool enabled))\n"); err == nil {
		t.Fatal("expected bool parameter to be rejected")
	}
}

func TestParseRejectsUnknownTypes(t *testing.T) {
	if _, err := Parse("GK_API(Scene, Add, void, (SomeStruct* thing))\n"); err == nil {
		t.Fatal("expected unknown type to be rejected")
	}
}

func TestParseRejectsNonConstPointerThatIsNotAnOutParameter(t *testing.T) {
	// A writable pointer that is not named out* has no defined ownership on the managed side.
	if _, err := Parse("GK_API(Scene, Fill, void, (FVec3* target))\n"); err == nil {
		t.Fatal("expected non-out pointer to be rejected")
	}
}

func TestGenerateProducesFriendlyWrappers(t *testing.T) {
	entries, err := Parse(`
GK_API(Log, Info, void, (GkStr message))
GK_API(UI, Begin, GkBool, (GkStr name, int32_t flags))   //= flags:0
GK_API(UI, GetScreenSize, void, (FVec2* outSize))
GK_API(UI, DrawText, void, (GkStr text, float x, float y, GkColor32 color, float scale))   //= scale:1.0f
GK_API(SceneBuild, AddSphereModel, uint32_t, (const FVec3* center, float radius))
GK_API(Paths, GetProjectRoot, int32_t, (char* buffer, int32_t capacity))
`)
	if err != nil {
		t.Fatalf("parse: %v", err)
	}

	generated, err := Generate(entries)
	if err != nil {
		t.Fatalf("generate: %v", err)
	}

	wants := []string{
		// the raw table keeps the ABI types
		"public delegate* unmanaged<GkStr, int, int> UI_Begin;",
		// strings become string and are encoded into the arena
		"public static void Info(string message)",
		"Utf8Arena.Encode(message)",
		// GkBool becomes bool, and a numeric default becomes a C# default argument
		"public static bool Begin(string name, int flags = 0)",
		// an out-vector becomes the return value
		"public static Vector2 GetScreenSize()",
		// packed color becomes the friendly Color struct
		"public static void DrawText(string text, float x, float y, Color color, float scale = 1.0f)",
		"color.ToPacked()",
		// const pointers become `in` parameters that are pinned at the call site
		"public static uint AddSphereModel(in Vector3 center, float radius)",
		"fixed (Vector3* p_center = &center)",
		// a buffer/capacity pair becomes a string return with a probe call
		"public static string GetProjectRoot()",
		"int required = Api.Table->Paths_GetProjectRoot(null, 0);",
		"EntryCount = 6",
	}
	for _, want := range wants {
		if !strings.Contains(generated, want) {
			t.Errorf("generated output is missing %q\n---\n%s", want, generated)
		}
	}
}

func TestGenerateRejectsDefaultsThatAreNotTrailing(t *testing.T) {
	entries, err := Parse("GK_API(UI, DrawText, void, (GkStr text, float scale, GkColor32 color))   //= scale:1.0f\n")
	if err != nil {
		t.Fatalf("parse: %v", err)
	}
	if _, err := Generate(entries); err == nil {
		t.Fatal("expected a non-trailing default to be rejected")
	}
}
