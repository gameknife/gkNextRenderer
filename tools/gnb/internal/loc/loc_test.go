package loc

import "testing"

func TestClassifyEngineModule(t *testing.T) {
	key, ok := classify("Engine/Runtime/Scripting/ScriptContext.cpp")
	if !ok {
		t.Fatal("expected Engine path to be classified")
	}
	if key.category != "Engine" || key.sub != "Runtime" || key.leaf != "Scripting" {
		t.Fatalf("unexpected key: %+v", key)
	}

	key, ok = classify("Engine/Runtime/Engine.cpp")
	if !ok {
		t.Fatal("expected Engine file to be classified")
	}
	if key.category != "Engine" || key.sub != "Runtime" || key.leaf != "" {
		t.Fatalf("direct child file should not become a leaf: %+v", key)
	}
}

func TestReportSnapshotExpandsOnlyLargeNonApplicationSubs(t *testing.T) {
	report := newReport()
	report.add(leafKey{category: "Engine", sub: "Runtime", leaf: "Scripting"}, expandedSubLines)
	report.add(leafKey{category: "Engine", sub: "Runtime", leaf: "Reflection"}, 1)
	report.add(leafKey{category: "Engine", sub: "Common", leaf: "Platform"}, expandedSubLines)
	report.add(leafKey{category: "Application", sub: "Game", leaf: "Flappy"}, 1)

	snapshot := reportToSnapshot(report, Options{})
	engine := findCategory(t, snapshot, "Engine")
	runtime := findSub(t, engine, "Runtime")
	if len(runtime.Leaves) != 2 {
		t.Fatalf("large Runtime sub should expose leaves, got %+v", runtime.Leaves)
	}
	common := findSub(t, engine, "Common")
	if len(common.Leaves) != 0 {
		t.Fatalf("5000-line Common sub should remain collapsed, got %+v", common.Leaves)
	}
	application := findCategory(t, snapshot, "Application")
	game := findSub(t, application, "Game")
	if len(game.Leaves) != 1 || game.Leaves[0].Name != "Flappy" {
		t.Fatalf("Application leaves should always be preserved, got %+v", game.Leaves)
	}
}

func findCategory(t *testing.T, snapshot *Snapshot, name string) CategorySummary {
	t.Helper()
	for _, category := range snapshot.Categories {
		if category.Name == name {
			return category
		}
	}
	t.Fatalf("category %q not found", name)
	return CategorySummary{}
}

func findSub(t *testing.T, category CategorySummary, name string) SubSummary {
	t.Helper()
	for _, sub := range category.Subs {
		if sub.Name == name {
			return sub
		}
	}
	t.Fatalf("sub %q not found in category %q", name, category.Name)
	return SubSummary{}
}
