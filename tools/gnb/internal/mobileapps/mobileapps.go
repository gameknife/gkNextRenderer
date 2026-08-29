// Package mobileapps reads the mobile application registry that CMake reads.
//
// Which applications can be packaged for Android or iOS, and the identity each one installs under,
// used to be duplicated between src/Application, tools/android and this CLI. They are now stated
// once in src/Application/MobileApplications.json, and everything that has to agree about them
// reads that file.
package mobileapps

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// Platform names an application can list. They match the JSON values, and the lowercase directory
// names CMake compares against.
const (
	Android = "android"
	IOS     = "ios"
)

// ManifestPath returns the registry location inside a checkout.
func ManifestPath(repoRoot string) string {
	return filepath.Join(repoRoot, "src", "Application", "MobileApplications.json")
}

// App is one entry of the registry. Only the fields the CLI needs are decoded; CMake owns the
// rest (directory, scene, .NET requirement).
type App struct {
	Target      string   `json:"target"`
	Label       string   `json:"label"`
	Platforms   []string `json:"platforms"`
	AndroidID   string   `json:"androidId"`
	IOSBundleID string   `json:"iosBundleId"`
}

// SupportsPlatform reports whether the application can be packaged for platform.
func (a App) SupportsPlatform(platform string) bool {
	for _, candidate := range a.Platforms {
		if strings.EqualFold(candidate, platform) {
			return true
		}
	}
	return false
}

type manifest struct {
	Applications []App `json:"applications"`
}

// Load reads every application in the registry, in manifest order.
func Load(repoRoot string) ([]App, error) {
	path := ManifestPath(repoRoot)
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read mobile application registry %s: %w", path, err)
	}
	parsed := manifest{}
	if err := json.Unmarshal(data, &parsed); err != nil {
		return nil, fmt.Errorf("parse mobile application registry %s: %w", path, err)
	}
	if len(parsed.Applications) == 0 {
		return nil, fmt.Errorf("mobile application registry %s lists no applications", path)
	}
	return parsed.Applications, nil
}

// ForPlatform reads the registry and keeps only the applications platform can package.
func ForPlatform(repoRoot, platform string) ([]App, error) {
	all, err := Load(repoRoot)
	if err != nil {
		return nil, err
	}
	filtered := make([]App, 0, len(all))
	for _, app := range all {
		if app.SupportsPlatform(platform) {
			filtered = append(filtered, app)
		}
	}
	if len(filtered) == 0 {
		return nil, fmt.Errorf("mobile application registry lists no %s applications", platform)
	}
	return filtered, nil
}

// Resolve maps a possibly-empty, possibly-differently-cased name onto its registry entry. An empty
// name selects the first application the manifest lists for the platform, which is what keeps the
// default out of the code.
func Resolve(repoRoot, platform, name string) (App, error) {
	candidates, err := ForPlatform(repoRoot, platform)
	if err != nil {
		return App{}, err
	}
	if strings.TrimSpace(name) == "" {
		return candidates[0], nil
	}
	for _, app := range candidates {
		if strings.EqualFold(app.Target, name) {
			return app, nil
		}
	}
	return App{}, fmt.Errorf("unsupported %s app %q (expected one of %s)",
		platform, name, strings.Join(Names(candidates), ", "))
}

// Names lists the target names of apps, in the order given.
func Names(apps []App) []string {
	names := make([]string, 0, len(apps))
	for _, app := range apps {
		names = append(names, app.Target)
	}
	return names
}

// FlagHelp renders the registry as the help text of an --app flag. Errors are swallowed on
// purpose: a broken registry has to fail the command that uses it, not the help output that
// happens to be built while cobra is still wiring up.
func FlagHelp(repoRoot, platform, description string) string {
	apps, err := ForPlatform(repoRoot, platform)
	if err != nil {
		return description
	}
	return fmt.Sprintf("%s (default %s; see src/Application/MobileApplications.json for all %d)",
		description, apps[0].Target, len(apps))
}
