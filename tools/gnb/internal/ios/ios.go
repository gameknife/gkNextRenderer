package ios

import (
	"bytes"
	"encoding/json"
	"encoding/xml"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/cmakerun"
)

const (
	preset = "ios-device"
	target = "gkNextRenderer"
	// artifactConfiguration must match the configuration of the `ios-device`
	// build preset in CMakePresets.json, which decides which manifest CMake
	// generates for the bundle gnb launches.
	artifactConfiguration = "RelWithDebInfo"
	// wrapperDirName keeps the Designed-for-iPad wrappers beside the raw bundle
	// without colliding with the CMake output layout.
	wrapperDirName = "DesignedForIpad"
)

// StagedApp locates the Designed-for-iPad app prepared for launch. An empty
// WrapperPath means nothing was staged because the build is unsigned.
type StagedApp struct {
	WrapperPath string
	// Restaged reports that the wrapper's inner iOS bundle was replaced.
	Restaged bool
}

// Build compiles the device bundle and, when it is signed, stages it as a
// Designed-for-iPad wrapper that macOS can launch.
func Build(repoRoot, cmakePath, teamID string, quiet bool, opts cmakerun.BuildOptions) (StagedApp, error) {
	if runtime.GOOS != "darwin" {
		return StagedApp{}, fmt.Errorf("iOS builds require macOS and Xcode; current host is %s", runtime.GOOS)
	}
	opts.Targets = []string{target}
	if quiet {
		opts.BuildToolArgs = append(opts.BuildToolArgs, "-quiet")
	}
	// Always write the sole signing input so a previously signed cache cannot
	// leak into a later unsigned build.
	opts.ConfigureArgs = append(opts.ConfigureArgs, "-DIOS_DEVELOPMENT_TEAM="+teamID)
	if err := cmakerun.BuildWithCMake(repoRoot, cmakePath, preset, opts); err != nil {
		return StagedApp{}, err
	}
	if teamID == "" {
		// An unsigned CI bundle is killed on launch, so staging it would only
		// copy the bundle for nothing.
		return StagedApp{}, nil
	}
	artifact, err := ReadArtifact(repoRoot)
	if err != nil {
		return StagedApp{}, err
	}
	return stageWrapper(artifact.BundlePath, dittoBundle, codesignCDHash)
}

// Artifact identifies the iOS application produced by the device build.
type Artifact struct {
	BundlePath string `json:"bundle_path"`
	BundleID   string `json:"bundle_id"`
	// StagedApp is filled in when the app is staged for launch, not read from
	// the manifest.
	StagedApp `json:"-"`
}

// Run launches the signed device application on an Apple Silicon Mac as an app
// designed for iPad.
func Run(repoRoot string) (Artifact, error) {
	if runtime.GOOS != "darwin" || runtime.GOARCH != "arm64" {
		return Artifact{}, fmt.Errorf("iOS app launch on this Mac requires macOS arm64; current host is %s/%s", runtime.GOOS, runtime.GOARCH)
	}
	return runOnMac(repoRoot, verifyCodeSignature, dittoBundle, codesignCDHash, launchWrapper)
}

func runOnMac(repoRoot string, verify func(string) error, copyBundle bundleCopier, hashBundle bundleHasher, launch func(string) error) (Artifact, error) {
	artifact, err := ReadArtifact(repoRoot)
	if err != nil {
		return Artifact{}, err
	}
	if err := verify(artifact.BundlePath); err != nil {
		return Artifact{}, fmt.Errorf("iOS app signature is not valid: %w\nrebuild with `gnb ios build --team-id <TEAM_ID>` before running", err)
	}
	// Restage when the build changed so a bundle rebuilt in Xcode cannot be
	// shadowed by an older wrapper copy.
	staged, err := stageWrapper(artifact.BundlePath, copyBundle, hashBundle)
	if err != nil {
		return Artifact{}, err
	}
	if err := launch(staged.WrapperPath); err != nil {
		return Artifact{}, fmt.Errorf("launch iOS app %s: %w", staged.WrapperPath, err)
	}
	artifact.StagedApp = staged
	return artifact, nil
}

type bundleCopier func(sourcePath, destinationPath string) error

type bundleHasher func(bundlePath string) (string, error)

// stageWrapper mirrors the signed iOS bundle into the Wrapper layout macOS
// requires for Designed-for-iPad apps. Launch Services refuses to spawn a bare
// iPhoneOS executable; only this layout gets the correct platform passed
// through posix_spawn, so a bare bundle is killed on sight (SIGKILL, code 9).
//
// An unchanged wrapper is reused. When the bundle changes, its inner copy is
// replaced while the outer wrapper and WrappedBundle link are retained. This
// keeps the Launch Services target stable without allowing an old inner app to
// shadow the freshly built bundle.
func stageWrapper(bundlePath string, copyBundle bundleCopier, hashBundle bundleHasher) (StagedApp, error) {
	bundleName := filepath.Base(bundlePath)
	wrapperPath := filepath.Join(filepath.Dir(bundlePath), wrapperDirName, bundleName)
	innerPath := filepath.Join(wrapperPath, "Wrapper", bundleName)
	linkPath := filepath.Join(wrapperPath, "WrappedBundle")
	if wrapperIsCurrent(bundlePath, innerPath, linkPath, hashBundle) {
		return StagedApp{WrapperPath: wrapperPath}, nil
	}

	if err := os.MkdirAll(filepath.Dir(innerPath), 0o755); err != nil {
		return StagedApp{}, fmt.Errorf("create Designed-for-iPad wrapper %s: %w", wrapperPath, err)
	}
	// Keep wrapperPath itself in place. Launch Services identifies this outer
	// app, whereas replacing only innerPath ensures it cannot retain an older
	// signed iOS bundle after a successful rebuild.
	if err := os.RemoveAll(innerPath); err != nil {
		return StagedApp{}, fmt.Errorf("clear previous iOS bundle in wrapper %s: %w", innerPath, err)
	}
	if err := copyBundle(bundlePath, innerPath); err != nil {
		return StagedApp{}, fmt.Errorf("copy %s into Designed-for-iPad wrapper: %w", bundlePath, err)
	}
	if err := ensureWrappedBundleLink(linkPath, bundleName); err != nil {
		return StagedApp{}, err
	}
	return StagedApp{WrapperPath: wrapperPath, Restaged: true}, nil
}

func ensureWrappedBundleLink(linkPath, bundleName string) error {
	want := filepath.Join("Wrapper", bundleName)
	if link, err := os.Readlink(linkPath); err == nil && link == want {
		return nil
	}
	if err := os.RemoveAll(linkPath); err != nil {
		return fmt.Errorf("clear WrappedBundle link %s: %w", linkPath, err)
	}
	if err := os.Symlink(want, linkPath); err != nil {
		return fmt.Errorf("link WrappedBundle in %s: %w", filepath.Dir(linkPath), err)
	}
	return nil
}

// wrapperIsCurrent reports whether a complete staged copy already carries the
// same code signature as the freshly built bundle.
func wrapperIsCurrent(bundlePath, innerPath, linkPath string, hashBundle bundleHasher) bool {
	if _, err := os.Stat(innerPath); err != nil {
		return false
	}
	// Lstat: a staging run interrupted before the symlink was created leaves an
	// incomplete wrapper that must not be reused.
	if _, err := os.Lstat(linkPath); err != nil {
		return false
	}
	if link, err := os.Readlink(linkPath); err != nil || link != filepath.Join("Wrapper", filepath.Base(bundlePath)) {
		return false
	}
	built, err := hashBundle(bundlePath)
	if err != nil || built == "" {
		return false
	}
	staged, err := hashBundle(innerPath)
	if err != nil {
		return false
	}
	return built == staged
}

// codesignCDHash returns the code directory hash identifying the exact bundle
// contents.
func codesignCDHash(bundlePath string) (string, error) {
	output, err := exec.Command("codesign", "-d", "--verbose=5", bundlePath).CombinedOutput()
	if err != nil {
		if detail := strings.TrimSpace(string(output)); detail != "" {
			return "", fmt.Errorf("%s", detail)
		}
		return "", err
	}
	return parseCDHash(string(output), bundlePath)
}

func parseCDHash(output, bundlePath string) (string, error) {
	for _, line := range strings.Split(output, "\n") {
		if hash, found := strings.CutPrefix(strings.TrimSpace(line), "CDHash="); found && hash != "" {
			return hash, nil
		}
	}
	return "", fmt.Errorf("codesign did not report a CDHash for %s", bundlePath)
}

// dittoBundle copies a signed bundle. ditto preserves the extended attributes
// the code signature seals, which a plain recursive copy can drop.
func dittoBundle(sourcePath, destinationPath string) error {
	output, err := exec.Command("ditto", sourcePath, destinationPath).CombinedOutput()
	if err == nil {
		return nil
	}
	if detail := strings.TrimSpace(string(output)); detail != "" {
		return fmt.Errorf("%s", detail)
	}
	return err
}

// ReadArtifact reads the manifest generated by CMake for the device bundle.
func ReadArtifact(repoRoot string) (Artifact, error) {
	manifestPath := filepath.Join(repoRoot, "out", "build", preset, "ios-app-"+artifactConfiguration+".json")
	data, err := os.ReadFile(manifestPath)
	if err != nil {
		if os.IsNotExist(err) {
			return Artifact{}, fmt.Errorf("iOS build artifact not found: %s\nrun `gnb ios build --team-id <TEAM_ID>` first", manifestPath)
		}
		return Artifact{}, fmt.Errorf("read iOS artifact manifest %s: %w", manifestPath, err)
	}

	artifact := Artifact{}
	if err := json.Unmarshal(data, &artifact); err != nil {
		return Artifact{}, fmt.Errorf("parse iOS artifact manifest %s: %w", manifestPath, err)
	}
	if artifact.BundlePath == "" || !filepath.IsAbs(artifact.BundlePath) || filepath.Ext(artifact.BundlePath) != ".app" {
		return Artifact{}, fmt.Errorf("invalid iOS bundle path in %s: %q", manifestPath, artifact.BundlePath)
	}
	info, err := os.Stat(artifact.BundlePath)
	if err != nil {
		return Artifact{}, fmt.Errorf("iOS app bundle not found: %s\nrun `gnb ios build --team-id <TEAM_ID>` first", artifact.BundlePath)
	}
	if !info.IsDir() {
		return Artifact{}, fmt.Errorf("iOS bundle path is not a directory: %s", artifact.BundlePath)
	}
	// Prefer what the built bundle declares over the manifest's generated value.
	if id, err := bundleIdentifier(artifact.BundlePath); err == nil && id != "" {
		artifact.BundleID = id
	}
	return artifact, nil
}

func verifyCodeSignature(bundlePath string) error {
	cmd := exec.Command("codesign", "--verify", "--strict", bundlePath)
	output, err := cmd.CombinedOutput()
	if err == nil {
		return nil
	}
	detail := strings.TrimSpace(string(output))
	if detail == "" {
		return err
	}
	return fmt.Errorf("%s", detail)
}

// launchWrapper hands the wrapper to Launch Services, which is what supplies
// the iOS platform identity the kernel checks when it spawns the executable.
// Launching the bare bundle instead gets the process killed (SIGKILL, code 9).
func launchWrapper(wrapperPath string) error {
	output, err := exec.Command("open", wrapperPath).CombinedOutput()
	if err == nil {
		return nil
	}
	if detail := strings.TrimSpace(string(output)); detail != "" {
		return fmt.Errorf("%s", detail)
	}
	return err
}

// bundleIdentifier reads the identifier the built bundle actually carries. The
// CMake manifest can drift from it when the Xcode project's
// PRODUCT_BUNDLE_IDENTIFIER is edited without reconfiguring.
func bundleIdentifier(bundlePath string) (string, error) {
	output, err := exec.Command("plutil", "-extract", "CFBundleIdentifier", "raw", "-o", "-", filepath.Join(bundlePath, "Info.plist")).Output()
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(string(output)), nil
}

// Team identifies an Apple Developer team available through a locally installed
// provisioning profile.
type Team struct {
	Name string
	ID   string
}

// ProvisioningProfileDirectories returns the profile locations used by Xcode
// and device deployment tools on macOS.
func ProvisioningProfileDirectories(homeDir string) []string {
	return []string{
		filepath.Join(homeDir, "Library", "Developer", "Xcode", "UserData", "Provisioning Profiles"),
		filepath.Join(homeDir, "Library", "MobileDevice", "Provisioning Profiles"),
	}
}

// Teams lists the distinct Apple Developer teams found in local provisioning
// profiles. A team must have a profile before it can sign an iOS application.
func Teams(homeDir string) ([]Team, []error) {
	return discoverTeams(ProvisioningProfileDirectories(homeDir), decodeProvisioningProfile)
}

type profileDecoder func(path string) (Team, error)

func discoverTeams(profileDirs []string, decode profileDecoder) ([]Team, []error) {
	teamsByID := make(map[string]Team)
	var errs []error

	for _, profileDir := range profileDirs {
		entries, err := os.ReadDir(profileDir)
		if err != nil {
			if os.IsNotExist(err) {
				continue
			}
			errs = append(errs, fmt.Errorf("read provisioning profiles in %s: %w", profileDir, err))
			continue
		}

		for _, entry := range entries {
			if entry.IsDir() || !strings.EqualFold(filepath.Ext(entry.Name()), ".mobileprovision") {
				continue
			}
			profilePath := filepath.Join(profileDir, entry.Name())
			team, err := decode(profilePath)
			if err != nil {
				errs = append(errs, fmt.Errorf("read provisioning profile %s: %w", profilePath, err))
				continue
			}
			if team.ID == "" {
				errs = append(errs, fmt.Errorf("read provisioning profile %s: missing TeamIdentifier", profilePath))
				continue
			}
			if existing, found := teamsByID[team.ID]; !found || existing.Name == "" {
				teamsByID[team.ID] = team
			}
		}
	}

	teams := make([]Team, 0, len(teamsByID))
	for _, team := range teamsByID {
		teams = append(teams, team)
	}
	sort.Slice(teams, func(i, j int) bool {
		if teams[i].Name == teams[j].Name {
			return teams[i].ID < teams[j].ID
		}
		return teams[i].Name < teams[j].Name
	})
	return teams, errs
}

func decodeProvisioningProfile(path string) (Team, error) {
	output, err := exec.Command("security", "cms", "-D", "-i", path).Output()
	if err != nil {
		return Team{}, err
	}
	return parseProvisioningProfile(output)
}

func parseProvisioningProfile(data []byte) (Team, error) {
	decoder := xml.NewDecoder(bytes.NewReader(data))
	team := Team{}

	for {
		token, err := decoder.Token()
		if err != nil {
			if err == io.EOF {
				break
			}
			return Team{}, fmt.Errorf("parse plist: %w", err)
		}
		start, ok := token.(xml.StartElement)
		if !ok || start.Name.Local != "key" {
			continue
		}

		var key string
		if err := decoder.DecodeElement(&key, &start); err != nil {
			return Team{}, fmt.Errorf("parse plist key: %w", err)
		}

		switch key {
		case "TeamName":
			value, err := nextPlistString(decoder)
			if err != nil {
				return Team{}, fmt.Errorf("parse TeamName: %w", err)
			}
			team.Name = value
		case "TeamIdentifier":
			value, err := nextPlistString(decoder)
			if err != nil {
				return Team{}, fmt.Errorf("parse TeamIdentifier: %w", err)
			}
			team.ID = value
		}
	}

	if team.ID == "" {
		return Team{}, fmt.Errorf("missing TeamIdentifier")
	}
	return team, nil
}

func nextPlistString(decoder *xml.Decoder) (string, error) {
	for {
		token, err := decoder.Token()
		if err != nil {
			return "", err
		}
		start, ok := token.(xml.StartElement)
		if !ok {
			continue
		}
		if start.Name.Local == "string" {
			var value string
			if err := decoder.DecodeElement(&value, &start); err != nil {
				return "", err
			}
			return value, nil
		}
	}
}
