package ios

import "github.com/gameknife/gknextrenderer/tools/gnb/internal/cmakerun"

const (
	preset = "ios-device"
	target = "gkNextRenderer"
)

func Build(repoRoot, cmakePath, teamID string, opts cmakerun.BuildOptions) error {
	opts.Targets = []string{target}
	// Always write the sole signing input so a previously signed cache cannot
	// leak into a later unsigned build.
	opts.ConfigureArgs = append(opts.ConfigureArgs, "-DIOS_DEVELOPMENT_TEAM="+teamID)
	return cmakerun.BuildWithCMake(repoRoot, cmakePath, preset, opts)
}
