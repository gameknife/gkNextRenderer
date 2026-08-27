package csharpgen

import (
	"os"
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
GK_API(Scene, SetNodeTransforms, void, (const FNodeTransform* transforms, int32_t transformCount))
GK_API(UI, SubmitDrawList, void, (const FUiDrawCommand* commands, int32_t commandCount, const FVec2* points, int32_t pointCount, const uint8_t* utf8, int32_t utf8Length))
GK_API(Physics, CreateBoxBody, uint32_t, (const FVec3* position, const FVec4* rotation, const FVec3* extent, GkPhysicsMotionType motionType))
GK_API(Physics, GetBodyState, void, (uint32_t bodyId, FPhysicsBodyState* outState))
GK_API(SceneBuild, BindPhysicsBody, GkBool, (uint32_t nodeId, uint32_t bodyId, GkNodeMobility mobility))
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
		// const pointer + count pairs become allocation-free spans and retain the two raw arguments
		"public static void SetNodeTransforms(System.ReadOnlySpan<NodeTransform> transforms)",
		"fixed (NodeTransform* p_transforms = transforms)",
		"Scene_SetNodeTransforms(p_transforms, transforms.Length)",
		"public static void SubmitDrawList(System.ReadOnlySpan<UiDrawCommand> commands, System.ReadOnlySpan<Vector2> points, System.ReadOnlySpan<byte> utf8)",
		"UI_SubmitDrawList(p_commands, commands.Length, p_points, points.Length, p_utf8, utf8.Length)",
		// fixed-width domain enums and out structs stay friendly without weakening the ABI
		"public static uint CreateBoxBody(in Vector3 position, in Vector4 rotation, in Vector3 extent, PhysicsMotionType motionType)",
		"public static PhysicsBodyState GetBodyState(uint bodyId)",
		"public static bool BindPhysicsBody(uint nodeId, uint bodyId, NodeMobility mobility)",
		// a buffer/capacity pair becomes a string return with a probe call
		"public static string GetProjectRoot()",
		"int required = Api.Table->Paths_GetProjectRoot(null, 0);",
		"EntryCount = 11",
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

func testManifest() Manifest {
	return Manifest{
		Version: 1,
		Types: []ReflectedType{
			{
				Name: "Node", TypeID: 879231789, Kind: "node",
				Properties: []ReflectedProperty{
					{Name: "Name", PropID: 1, Type: "String", ReadOnly: true, ScriptExposed: true,
						Tooltip: "Node name", Category: "Transform"},
					{Name: "Translation", PropID: 2, Type: "Vec3", ScriptExposed: true},
				},
			},
			{
				Name: "RenderComponent", TypeID: 3393791048, Kind: "component",
				Properties: []ReflectedProperty{
					{Name: "Visible", PropID: 3, Type: "Bool", ScriptExposed: true},
					{Name: "Materials", PropID: 4, Type: "Array", ScriptExposed: true},
					{Name: "Secret", PropID: 5, Type: "Float"},
				},
			},
		},
	}
}

func TestGenerateComponentsProducesWrappers(t *testing.T) {
	generated, err := GenerateComponents(testManifest())
	if err != nil {
		t.Fatalf("generate: %v", err)
	}

	wants := []string{
		// ids are compile-time constants; that is the whole point of the manifest
		"public const uint TypeId = 3393791048u;",
		"public readonly struct RenderComponent(uint nodeId) : IComponentRef<RenderComponent>",
		// a settable property gets both accessors, pointed at the matching binding pair
		"get => Component.GetBool(Id, TypeId, 3u);",
		"set => Component.SetBool(Id, TypeId, 3u, value);",
		// a read-only property gets no setter
		"public string Name => Component.GetString(Id, TypeId, 1u);",
		// vectors are passed by reference
		"set => Component.SetVec3(Id, TypeId, 2u, in value);",
		// the node's own properties live on NodeRef, and components hang off it
		"public readonly struct NodeRef(uint nodeId)",
		"public RenderComponent Render => new(Id);",
		// what the plan's acceptance line needs
		"public T GetComponent<T>() where T : struct, IComponentRef<T> => T.FromNode(Id);",
		// gaps are stated in the generated file rather than silently dropped
		"//   Materials (Array) — arrays need element-level accessors, not a value copy",
		"//   Secret — not script-exposed",
	}
	for _, want := range wants {
		if !strings.Contains(generated, want) {
			t.Errorf("generated output is missing %q\n---\n%s", want, generated)
		}
	}
}

func TestGenerateComponentsRejectsAnUnhandledPropertyType(t *testing.T) {
	manifest := testManifest()
	manifest.Types[1].Properties[0].Type = "SomethingNew"
	// A new PropertyType must be handled deliberately — either bound or listed as a known gap.
	// Falling through and emitting nothing would hide a whole property from script authors.
	if _, err := GenerateComponents(manifest); err == nil {
		t.Fatal("expected an unhandled property type to be rejected")
	}
}

func TestGenerateComponentsRejectsAShorthandCollision(t *testing.T) {
	manifest := testManifest()
	manifest.Types[0].Properties = append(manifest.Types[0].Properties,
		ReflectedProperty{Name: "Render", PropID: 9, Type: "Bool", ScriptExposed: true})
	// "RenderComponent" shortens to "Render", which would now be declared twice on NodeRef.
	if _, err := GenerateComponents(manifest); err == nil {
		t.Fatal("expected a shorthand collision to be rejected")
	}
}

func TestParseManifestRejectsAnUnknownKind(t *testing.T) {
	path := t.TempDir() + "/manifest.json"
	if err := os.WriteFile(path, []byte(`{"version":1,"types":[{"name":"X","kind":"widget","properties":[]}]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := ParseManifest(path); err == nil {
		t.Fatal("expected an unknown kind to be rejected")
	}
}
