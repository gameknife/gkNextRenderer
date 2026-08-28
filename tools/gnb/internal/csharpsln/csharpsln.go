// Package csharpsln generates the solution file that makes assets/csharp openable in an IDE.
//
// Everything the managed layer needs to *build* already lives in the csproj files, and CMake drives
// them one project at a time. An IDE works the other way round: with no solution it has no reason
// to load GkNext.Engine or the source generator when a game project is opened, so the game's types
// resolve against nothing and the editor degrades to plain text. This file is that missing entry
// point, and it is generated rather than hand-maintained so a new C# game cannot silently be left
// out of it.
package csharpsln

import (
	"crypto/sha1"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

// ManagedRoot holds every managed project; SolutionPath is the generated entry point.
const (
	ManagedRoot  = "assets/csharp"
	SolutionPath = "assets/csharp/GkNextManaged.sln"
)

// Solution folders. The order they are emitted in is fixed so the generated file is stable.
const (
	engineFolder = "Engine"
	gamesFolder  = "Games"
)

// Well-known type GUIDs from the .sln format: one for C# projects, one for solution folders.
const (
	csharpProjectType = "{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}"
	folderProjectType = "{2150E333-8FDC-42A3-9474-1A3956D46DE8}"
)

// A .sln starts with a UTF-8 BOM and a blank line; Visual Studio rewrites the file if either is
// missing, which would turn every solution load into a pending diff.
const byteOrderMark = "\uFEFF"

// Every managed assembly is architecture neutral — the native host decides the bitness — so all of
// these map onto Any CPU. They are declared anyway because an IDE whose active platform is x64
// otherwise builds into bin/x64/, which is not where the publish rules look.
var solutionPlatforms = []string{"Any CPU", "x64", "x86"}

var configurations = []string{"Debug", "Release"}

// Project is one csproj as the solution sees it.
type Project struct {
	Name    string // GkNext.Engine
	RelPath string // GkNext.Engine\GkNext.Engine.csproj, relative to the solution
	Folder  string // engineFolder or gamesFolder
	GUID    string
}

// Result reports what a Run call did.
type Result struct {
	Projects []Project
	Changed  bool
}

// Run regenerates SolutionPath from the csproj files on disk. With check set it only reports
// whether the file on disk is up to date, which is what CI wants.
func Run(repoRoot string, check bool) (Result, error) {
	projects, err := Discover(repoRoot)
	if err != nil {
		return Result{}, err
	}
	if len(projects) == 0 {
		return Result{}, fmt.Errorf("no csproj found under %s", ManagedRoot)
	}

	content := Render(projects)
	path := filepath.Join(repoRoot, filepath.FromSlash(SolutionPath))
	existing, readErr := os.ReadFile(path)
	// Compared with the BOM and line endings normalised, the same way csharpgen does: git may hand
	// the file back in either shape and a spurious "out of date" would make the check useless.
	result := Result{
		Projects: projects,
		Changed:  readErr != nil || normalize(string(existing)) != normalize(content),
	}

	if check {
		if result.Changed {
			return result, fmt.Errorf("%s is out of date; run 'gnb dotnet sln'", SolutionPath)
		}
		return result, nil
	}
	if !result.Changed {
		return result, nil
	}
	return result, os.WriteFile(path, []byte(content), 0o644)
}

// Discover finds every managed project, ordered the way the solution lists them.
func Discover(repoRoot string) ([]Project, error) {
	root := filepath.Join(repoRoot, filepath.FromSlash(ManagedRoot))
	var projects []Project

	err := filepath.WalkDir(root, func(path string, entry fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if entry.IsDir() {
			if isBuildDir(entry.Name()) || isScratchDir(entry.Name()) {
				return fs.SkipDir
			}
			return nil
		}
		if filepath.Ext(entry.Name()) != ".csproj" {
			return nil
		}
		relative, relErr := filepath.Rel(root, path)
		if relErr != nil {
			return relErr
		}
		slashed := filepath.ToSlash(relative)
		isGame, gameErr := declaresGameInstance(filepath.Dir(path))
		if gameErr != nil {
			return gameErr
		}
		folder := engineFolder
		if isGame {
			folder = gamesFolder
		}
		projects = append(projects, Project{
			Name:    strings.TrimSuffix(entry.Name(), ".csproj"),
			RelPath: filepath.FromSlash(slashed),
			Folder:  folder,
			GUID:    deterministicGUID(slashed),
		})
		return nil
	})
	if err != nil {
		return nil, err
	}

	sort.Slice(projects, func(i, j int) bool {
		if projects[i].Folder != projects[j].Folder {
			return projects[i].Folder == engineFolder
		}
		return projects[i].Name < projects[j].Name
	})
	return projects, nil
}

// declaresGameInstance decides which solution folder a project belongs in by looking for the
// attribute that makes an assembly a game. Getting this wrong only moves a node in the tree, so a
// plain text scan is enough: the attribute always sits on its own line above the class.
func declaresGameInstance(projectDir string) (bool, error) {
	found := false
	err := filepath.WalkDir(projectDir, func(path string, entry fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if entry.IsDir() {
			if isBuildDir(entry.Name()) {
				return fs.SkipDir
			}
			return nil
		}
		if filepath.Ext(entry.Name()) != ".cs" {
			return nil
		}
		source, readErr := os.ReadFile(path)
		if readErr != nil {
			return readErr
		}
		for _, line := range strings.Split(string(source), "\n") {
			if strings.TrimSpace(line) == "[GameInstance]" {
				found = true
				return fs.SkipAll
			}
		}
		return nil
	})
	return found, err
}

// isScratchDir reports the throwaway projects `gnb dotnet templates` instantiates to check that
// the shipped game templates still compile. They live under assets/csharp because the generated
// csproj reaches GkNext.Engine by relative path, and they are deleted as soon as the check is
// done — but an interrupted run must not leave a phantom project in the IDE solution.
func isScratchDir(name string) bool {
	return strings.HasPrefix(name, "_templatecheck_")
}

// bin/ and obj/ hold NuGet's restore copies of the project file and generated sources; walking
// into them would list projects that do not exist.
func isBuildDir(name string) bool {
	return name == "bin" || name == "obj"
}

// deterministicGUID derives a project's solution GUID from its path so that regenerating the file
// on any machine produces the same bytes. A random GUID would make every regeneration a diff, and
// would reshuffle the IDE's per-project state along with it.
func deterministicGUID(seed string) string {
	sum := sha1.Sum([]byte("gknext-managed-solution:" + strings.ToLower(seed)))
	// RFC 4122 version 5, variant 1: the shape a name-based UUID has, so tooling that validates
	// the format accepts it.
	sum[6] = (sum[6] & 0x0f) | 0x50
	sum[8] = (sum[8] & 0x3f) | 0x80
	return strings.ToUpper(fmt.Sprintf("{%x-%x-%x-%x-%x}", sum[0:4], sum[4:6], sum[6:8], sum[8:10], sum[10:16]))
}

// Render writes the classic .sln text: line oriented, tab indented, CRLF terminated.
func Render(projects []Project) string {
	var out strings.Builder
	line := func(format string, args ...any) {
		fmt.Fprintf(&out, format+"\r\n", args...)
	}

	out.WriteString(byteOrderMark + "\r\n")
	line("Microsoft Visual Studio Solution File, Format Version 12.00")
	line("# Visual Studio Version 17")
	line("VisualStudioVersion = 17.0.31903.59")
	line("MinimumVisualStudioVersion = 10.0.40219.1")
	line("# Generated by 'gnb dotnet sln'. Do not edit by hand.")

	for _, folder := range []string{engineFolder, gamesFolder} {
		if !hasFolder(projects, folder) {
			continue
		}
		// Not %q: a project path is Windows separated and %q would escape every backslash.
		line(`Project("%s") = "%s", "%s", "%s"`, folderProjectType, folder, folder, folderGUID(folder))
		line("EndProject")
		for _, project := range projects {
			if project.Folder != folder {
				continue
			}
			line(`Project("%s") = "%s", "%s", "%s"`, csharpProjectType, project.Name, project.RelPath, project.GUID)
			line("EndProject")
		}
	}

	line("Global")
	line("\tGlobalSection(SolutionConfigurationPlatforms) = preSolution")
	for _, configuration := range configurations {
		for _, platform := range solutionPlatforms {
			line("\t\t%s|%s = %s|%s", configuration, platform, configuration, platform)
		}
	}
	line("\tEndGlobalSection")

	line("\tGlobalSection(ProjectConfigurationPlatforms) = postSolution")
	for _, project := range projects {
		for _, configuration := range configurations {
			for _, platform := range solutionPlatforms {
				line("\t\t%s.%s|%s.ActiveCfg = %s|Any CPU", project.GUID, configuration, platform, configuration)
				line("\t\t%s.%s|%s.Build.0 = %s|Any CPU", project.GUID, configuration, platform, configuration)
			}
		}
	}
	line("\tEndGlobalSection")

	line("\tGlobalSection(SolutionProperties) = preSolution")
	line("\t\tHideSolutionNode = FALSE")
	line("\tEndGlobalSection")

	line("\tGlobalSection(NestedProjects) = preSolution")
	for _, project := range projects {
		line("\t\t%s = %s", project.GUID, folderGUID(project.Folder))
	}
	line("\tEndGlobalSection")
	line("EndGlobal")

	return out.String()
}

func hasFolder(projects []Project, folder string) bool {
	for _, project := range projects {
		if project.Folder == folder {
			return true
		}
	}
	return false
}

func folderGUID(folder string) string {
	return deterministicGUID("folder:" + folder)
}

func normalize(text string) string {
	return strings.ReplaceAll(strings.TrimPrefix(text, byteOrderMark), "\r\n", "\n")
}
