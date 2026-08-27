package ios

import (
	"bufio"
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
	"strconv"
	"strings"
	"text/tabwriter"

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
	macDeviceID    = "mac"
	lsregisterPath = "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
)

type DeviceKind string

const (
	DeviceKindMac    DeviceKind = "mac"
	DeviceKindRemote DeviceKind = "remote"
)

// Device identifies a local Mac or a paired physical iOS device that can
// receive the built application.
type Device struct {
	Identifier string
	Kind       DeviceKind
	Name       string
	Model      string
	OSVersion  string
	UDID       string
}

func (d Device) IsMac() bool {
	return d.Kind == DeviceKindMac
}

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
	opts = withAutomaticProvisioningUpdates(teamID, opts)
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

func withAutomaticProvisioningUpdates(teamID string, opts cmakerun.BuildOptions) cmakerun.BuildOptions {
	if teamID != "" {
		// Let command-line Xcode perform the same automatic signing refresh as
		// the Xcode UI, including updating expired provisioning profiles.
		opts.BuildToolArgs = append(opts.BuildToolArgs, "-allowProvisioningUpdates")
	}
	return opts
}

// Artifact identifies the iOS application produced by the device build.
type Artifact struct {
	BundlePath string `json:"bundle_path"`
	BundleID   string `json:"bundle_id"`
	// StagedApp is filled in when the app is staged for launch, not read from
	// the manifest.
	StagedApp `json:"-"`
}

// Run launches the signed application on the requested device. When no device
// is requested, a single available device is selected automatically and an
// interactive choice is shown when multiple devices are available.
func Run(repoRoot, requestedDevice string, input io.Reader, output io.Writer) (Artifact, Device, error) {
	if runtime.GOOS != "darwin" || runtime.GOARCH != "arm64" {
		return Artifact{}, Device{}, fmt.Errorf("iOS app launch requires macOS arm64; current host is %s/%s", runtime.GOOS, runtime.GOARCH)
	}
	devices, err := Devices()
	if err != nil {
		return Artifact{}, Device{}, err
	}
	selected, err := selectDevice(devices, requestedDevice, input, output)
	if err != nil {
		return Artifact{}, Device{}, err
	}
	if selected.IsMac() {
		artifact, err := runOnMac(repoRoot, verifyCodeSignature, dittoBundle, codesignCDHash, launchWrapper)
		return artifact, selected, err
	}

	artifact, err := runOnPhysicalDevice(repoRoot, selected.Identifier)
	if err != nil {
		return Artifact{}, Device{}, err
	}
	return artifact, selected, nil
}

// ListDevices writes the available iOS run targets. CoreDevice's JSON output
// is used instead of parsing its human-readable table, whose columns are not
// a stable scripting interface.
func ListDevices(output io.Writer) error {
	devices, err := Devices()
	if err != nil {
		return err
	}
	if output == nil {
		output = io.Discard
	}
	if len(devices) == 0 {
		fmt.Fprintln(output, "No available iOS run devices found.")
		return nil
	}

	fmt.Fprintln(output, "Available iOS run devices:")
	writer := tabwriter.NewWriter(output, 0, 4, 2, ' ', 0)
	fmt.Fprintln(writer, "#\tID\tName\tModel\tOS")
	for index, device := range devices {
		model := device.Model
		if device.IsMac() {
			model = "Apple Silicon Mac"
		}
		osVersion := device.OSVersion
		if osVersion == "" {
			osVersion = "host"
		}
		fmt.Fprintf(writer, "%d\t%s\t%s\t%s\t%s\n", index+1, device.Identifier, device.Name, model, osVersion)
	}
	return writer.Flush()
}

// Devices returns the Mac Designed-for-iPad target plus paired, reachable
// physical iOS devices known by CoreDevice.
func Devices() ([]Device, error) {
	if runtime.GOOS != "darwin" {
		return nil, fmt.Errorf("iOS devices require macOS; current host is %s", runtime.GOOS)
	}

	devices := []Device{{
		Identifier: macDeviceID,
		Kind:       DeviceKindMac,
		Name:       "Mac (Designed for iPad)",
	}}
	physicalDevices, err := coreDevices()
	if err != nil {
		return nil, err
	}
	devices = append(devices, physicalDevices...)
	return devices, nil
}

type coreDeviceListResponse struct {
	Info struct {
		Outcome string `json:"outcome"`
	} `json:"info"`
	Result struct {
		Devices []coreDevice `json:"devices"`
	} `json:"result"`
}

type coreDevice struct {
	Identifier           string `json:"identifier"`
	ConnectionProperties struct {
		PairingState  string `json:"pairingState"`
		TransportType string `json:"transportType"`
	} `json:"connectionProperties"`
	DeviceProperties struct {
		Name            string `json:"name"`
		OSVersionNumber string `json:"osVersionNumber"`
	} `json:"deviceProperties"`
	HardwareProperties struct {
		Platform      string `json:"platform"`
		Reality       string `json:"reality"`
		MarketingName string `json:"marketingName"`
		UDID          string `json:"udid"`
	} `json:"hardwareProperties"`
}

func coreDevices() ([]Device, error) {
	if _, err := exec.LookPath("xcrun"); err != nil {
		return nil, fmt.Errorf("xcrun not found; install Xcode command-line tools: %w", err)
	}

	file, err := os.CreateTemp("", "gknext-devices-*.json")
	if err != nil {
		return nil, fmt.Errorf("create devicectl JSON output file: %w", err)
	}
	jsonPath := file.Name()
	defer os.Remove(jsonPath)
	if err := file.Close(); err != nil {
		return nil, fmt.Errorf("close devicectl JSON output file: %w", err)
	}

	cmd := exec.Command("xcrun", "devicectl", "list", "devices", "--json-output", jsonPath)
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		detail := strings.TrimSpace(stderr.String())
		if detail != "" {
			return nil, fmt.Errorf("list iOS devices with devicectl: %w\n%s", err, detail)
		}
		return nil, fmt.Errorf("list iOS devices with devicectl: %w", err)
	}

	data, err := os.ReadFile(jsonPath)
	if err != nil {
		return nil, fmt.Errorf("read devicectl device list: %w", err)
	}
	return parseCoreDevices(data)
}

func parseCoreDevices(data []byte) ([]Device, error) {
	response := coreDeviceListResponse{}
	if err := json.Unmarshal(data, &response); err != nil {
		return nil, fmt.Errorf("parse devicectl device list: %w", err)
	}
	if response.Info.Outcome != "" && response.Info.Outcome != "success" {
		return nil, fmt.Errorf("devicectl device list outcome: %s", response.Info.Outcome)
	}

	devices := make([]Device, 0, len(response.Result.Devices))
	for _, device := range response.Result.Devices {
		if !isAvailableCoreDevice(device) {
			continue
		}
		name := device.DeviceProperties.Name
		if name == "" {
			name = device.HardwareProperties.MarketingName
		}
		devices = append(devices, Device{
			Identifier: device.Identifier,
			Kind:       DeviceKindRemote,
			Name:       name,
			Model:      device.HardwareProperties.MarketingName,
			OSVersion:  device.DeviceProperties.OSVersionNumber,
			UDID:       device.HardwareProperties.UDID,
		})
	}
	sort.Slice(devices, func(i, j int) bool {
		if devices[i].Name == devices[j].Name {
			return devices[i].Identifier < devices[j].Identifier
		}
		return devices[i].Name < devices[j].Name
	})
	return devices, nil
}

func isAvailableCoreDevice(device coreDevice) bool {
	return device.Identifier != "" &&
		strings.EqualFold(device.HardwareProperties.Platform, "iOS") &&
		strings.EqualFold(device.HardwareProperties.Reality, "physical") &&
		strings.EqualFold(device.ConnectionProperties.PairingState, "paired") &&
		device.ConnectionProperties.TransportType != ""
}

func selectDevice(devices []Device, requested string, input io.Reader, output io.Writer) (Device, error) {
	requested = strings.TrimSpace(requested)
	if requested != "" {
		if index, err := strconv.Atoi(requested); err == nil {
			if index < 1 || index > len(devices) {
				return Device{}, fmt.Errorf("iOS device number %q is invalid; choose 1-%d", requested, len(devices))
			}
			return devices[index-1], nil
		}
		matches := make([]Device, 0, 1)
		for _, device := range devices {
			if requested == device.Identifier || requested == device.Name || requested == device.UDID {
				matches = append(matches, device)
			}
		}
		if len(matches) == 1 {
			return matches[0], nil
		}
		if len(matches) > 1 {
			return Device{}, fmt.Errorf("iOS device selector %q matches multiple devices; use the device ID", requested)
		}
		return Device{}, fmt.Errorf("iOS device %q is not available; run `gnb ios device` to list devices", requested)
	}
	if len(devices) == 0 {
		return Device{}, fmt.Errorf("no available iOS run device found; run `gnb ios device` to inspect devices")
	}
	if len(devices) == 1 {
		return devices[0], nil
	}
	if input == nil {
		return Device{}, fmt.Errorf("multiple iOS devices are available; pass `gnb ios run --device <ID>`")
	}
	if output == nil {
		output = io.Discard
	}

	fmt.Fprintln(output, "Multiple iOS run devices are available:")
	for index, device := range devices {
		fmt.Fprintf(output, "  %d) %s (%s)\n", index+1, device.Name, device.Identifier)
	}
	fmt.Fprint(output, "Select device [1]: ")
	line, err := bufio.NewReader(input).ReadString('\n')
	if err != nil && err != io.EOF {
		return Device{}, fmt.Errorf("read iOS device selection: %w", err)
	}
	choice := strings.TrimSpace(line)
	if choice == "" {
		return devices[0], nil
	}
	index, err := strconv.Atoi(choice)
	if err != nil || index < 1 || index > len(devices) {
		return Device{}, fmt.Errorf("invalid iOS device selection %q; choose 1-%d", choice, len(devices))
	}
	return devices[index-1], nil
}

func runOnPhysicalDevice(repoRoot, deviceIdentifier string) (Artifact, error) {
	artifact, err := ReadArtifact(repoRoot)
	if err != nil {
		return Artifact{}, err
	}
	if err := verifyCodeSignature(artifact.BundlePath); err != nil {
		return Artifact{}, fmt.Errorf("iOS app signature is not valid: %w\nrebuild with `gnb ios build --team-id <TEAM_ID>` before running", err)
	}
	if err := runDevicectl("device", "install", "app", "--device", deviceIdentifier, artifact.BundlePath); err != nil {
		return Artifact{}, fmt.Errorf("install iOS app on %s: %w", deviceIdentifier, err)
	}
	if err := runDevicectl("device", "process", "launch", "--device", deviceIdentifier, "--terminate-existing", artifact.BundleID); err != nil {
		return Artifact{}, fmt.Errorf("launch iOS app on %s: %w", deviceIdentifier, err)
	}
	return artifact, nil
}

func runDevicectl(args ...string) error {
	cmd := exec.Command("xcrun", append([]string{"devicectl"}, args...)...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return err
	}
	return nil
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
	// Refresh the Launch Services record first. The wrapper directory is reused
	// across builds, so Launch Services keeps serving the bundle identifier it
	// first saw at this path. When a rebuild changes the identifier, that stale
	// record makes the kernel spawn the app under the previous identity, and
	// AMFI rejects it: "Launch Constraint Violation", surfaced as a launchd
	// spawn failure (POSIX 162) with a valid signature and profile.
	//
	// Best effort: when the record is already correct this changes nothing, and
	// a registration failure should not block a launch that would succeed.
	_ = exec.Command(lsregisterPath, "-f", wrapperPath).Run()

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
