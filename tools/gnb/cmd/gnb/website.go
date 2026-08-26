package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/spf13/cobra"
)

func findNpmExecutable() (string, error) {
	npmName := "npm"
	if runtime.GOOS == "windows" {
		npmName = "npm.cmd"
	}
	path, err := exec.LookPath(npmName)
	if err != nil {
		return "", fmt.Errorf("Node.js / npm not found in PATH. Please install Node.js (https://nodejs.org) to manage the website")
	}
	return path, nil
}

func ensureWebsiteDependencies(websiteDir string) error {
	nodeModules := filepath.Join(websiteDir, "node_modules")
	if _, err := os.Stat(nodeModules); err == nil {
		return nil
	}

	npmPath, err := findNpmExecutable()
	if err != nil {
		return err
	}

	console.Info("Installing website dependencies with npm...")
	cmd := exec.Command(npmPath, "install")
	cmd.Dir = websiteDir
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Stdin = os.Stdin
	return cmd.Run()
}

func runWebsiteNpmScript(websiteDir string, script string, extraEnv map[string]string, extraArgs ...string) error {
	if err := ensureWebsiteDependencies(websiteDir); err != nil {
		return err
	}

	npmPath, err := findNpmExecutable()
	if err != nil {
		return err
	}

	args := []string{"run", script}
	if len(extraArgs) > 0 {
		args = append(args, "--")
		args = append(args, extraArgs...)
	}

	cmd := exec.Command(npmPath, args...)
	cmd.Dir = websiteDir
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Stdin = os.Stdin

	cmd.Env = os.Environ()
	for k, v := range extraEnv {
		cmd.Env = append(cmd.Env, fmt.Sprintf("%s=%s", k, v))
	}

	return cmd.Run()
}

func newWebsiteCommand(ctx appContext) *cobra.Command {
	websiteDir := filepath.Join(ctx.repoRoot, "website")
	var githubPagesMode bool
	var port int

	root := &cobra.Command{
		Use:     "website [command]",
		Aliases: []string{"site"},
		Short:   "Develop, build, and preview the gkNextEngine official website",
		Long: "Develop, build, and preview the gkNextEngine official website (VitePress).\n\n" +
			"Examples:\n" +
			"  gnb website             # Launch local dev server (hot-reload at http://localhost:5173)\n" +
			"  gnb website dev         # Same as `gnb website`\n" +
			"  gnb website build       # Compile pure static assets into website/.vitepress/dist\n" +
			"  gnb website preview     # Preview built static production website (http://localhost:4173)\n" +
			"  gnb website --pages     # Build & preview simulating GitHub Pages subpath (/gkNextEngine/)",
		RunE: func(cmd *cobra.Command, args []string) error {
			extraEnv := map[string]string{}
			if githubPagesMode {
				extraEnv["GITHUB_PAGES"] = "true"
			}
			var extraArgs []string
			if port > 0 {
				extraArgs = append(extraArgs, "--port", fmt.Sprintf("%d", port))
			}
			return runWebsiteNpmScript(websiteDir, "dev", extraEnv, extraArgs...)
		},
	}

	root.Flags().BoolVar(&githubPagesMode, "pages", false, "simulate GitHub Pages environment (base: /gkNextEngine/)")
	root.Flags().IntVarP(&port, "port", "p", 0, "specify local HTTP server port")

	devCmd := &cobra.Command{
		Use:   "dev",
		Short: "Start local development server with hot module reload",
		RunE: func(cmd *cobra.Command, args []string) error {
			extraEnv := map[string]string{}
			if githubPagesMode {
				extraEnv["GITHUB_PAGES"] = "true"
			}
			var extraArgs []string
			if port > 0 {
				extraArgs = append(extraArgs, "--port", fmt.Sprintf("%d", port))
			}
			return runWebsiteNpmScript(websiteDir, "dev", extraEnv, extraArgs...)
		},
	}
	devCmd.Flags().BoolVar(&githubPagesMode, "pages", false, "simulate GitHub Pages environment (base: /gkNextEngine/)")
	devCmd.Flags().IntVarP(&port, "port", "p", 0, "specify local HTTP server port")

	buildCmd := &cobra.Command{
		Use:   "build",
		Short: "Build pure static SSG production website",
		RunE: func(cmd *cobra.Command, args []string) error {
			extraEnv := map[string]string{}
			if githubPagesMode {
				extraEnv["GITHUB_PAGES"] = "true"
			}
			console.Info("Building static website with VitePress...")
			return runWebsiteNpmScript(websiteDir, "build", extraEnv)
		},
	}
	buildCmd.Flags().BoolVar(&githubPagesMode, "pages", false, "build with GitHub Pages base path (/gkNextEngine/)")

	previewCmd := &cobra.Command{
		Use:   "preview",
		Short: "Preview the built static website locally",
		RunE: func(cmd *cobra.Command, args []string) error {
			extraEnv := map[string]string{}
			if githubPagesMode {
				extraEnv["GITHUB_PAGES"] = "true"
			}
			var extraArgs []string
			if port > 0 {
				extraArgs = append(extraArgs, "--port", fmt.Sprintf("%d", port))
			}
			return runWebsiteNpmScript(websiteDir, "preview", extraEnv, extraArgs...)
		},
	}
	previewCmd.Flags().BoolVar(&githubPagesMode, "pages", false, "simulate GitHub Pages environment (base: /gkNextEngine/)")
	previewCmd.Flags().IntVarP(&port, "port", "p", 0, "specify local HTTP server port")

	setupCmd := &cobra.Command{
		Use:     "setup",
		Aliases: []string{"install"},
		Short:   "Install or update website Node dependencies",
		RunE: func(cmd *cobra.Command, args []string) error {
			npmPath, err := findNpmExecutable()
			if err != nil {
				return err
			}
			console.Info("Installing/updating website dependencies...")
			c := exec.Command(npmPath, "install")
			c.Dir = websiteDir
			c.Stdout = os.Stdout
			c.Stderr = os.Stderr
			c.Stdin = os.Stdin
			return c.Run()
		},
	}

	root.AddCommand(devCmd)
	root.AddCommand(buildCmd)
	root.AddCommand(previewCmd)
	root.AddCommand(setupCmd)

	return root
}
