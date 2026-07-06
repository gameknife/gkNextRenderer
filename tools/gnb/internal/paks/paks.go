package paks

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/fetcher"
)

func List(repoRoot string, cfg config.Config) error {
	for _, asset := range cfg.Paks.Assets {
		path := filepath.Join(repoRoot, asset.Dest)
		status := "missing"
		if info, err := os.Stat(path); err == nil {
			status = fmt.Sprintf("%d bytes", info.Size())
		}
		fmt.Printf("%-8s %-16s %s (%s)\n", asset.ID, asset.Name, asset.Dest, status)
	}
	return nil
}

func Fetch(repoRoot string, cfg config.Config, groups []string, force bool) error {
	selected := selectedGroups(cfg, groups)
	baseURL := os.Getenv("PAKS_BASE_URL")
	if baseURL == "" {
		repo := cfg.Paks.Repo
		if env := os.Getenv("PAKS_REPO"); env != "" {
			repo = env
		}
		tag := cfg.Paks.ReleaseTag
		if env := os.Getenv("PAKS_RELEASE_TAG"); env != "" {
			tag = env
		}
		baseURL = fmt.Sprintf("https://github.com/%s/releases/download/%s", repo, tag)
	}
	for _, asset := range cfg.Paks.Assets {
		if !selected[asset.ID] {
			continue
		}
		dst := filepath.Join(repoRoot, asset.Dest)
		if _, err := os.Stat(dst); err == nil && !force {
			console.Info("%s already exists", asset.Dest)
			continue
		}
		if err := fetcher.Download(baseURL+"/"+asset.Name, dst); err != nil {
			return err
		}
	}
	return nil
}

func Publish(repoRoot string, cfg config.Config, groups []string, dryRun bool, token string) error {
	if token == "" {
		token = os.Getenv("GITHUB_TOKEN")
	}
	if token == "" {
		return fmt.Errorf("GITHUB_TOKEN is required for `gnb paks publish`")
	}
	selected := selectedGroups(cfg, groups)
	assets := make([]config.PakAsset, 0)
	for _, asset := range cfg.Paks.Assets {
		if !selected[asset.ID] {
			continue
		}
		path := filepath.Join(repoRoot, asset.Dest)
		if _, err := os.Stat(path); err != nil {
			return fmt.Errorf("missing local asset: %s", asset.Dest)
		}
		assets = append(assets, asset)
	}
	if dryRun {
		for _, asset := range assets {
			console.Info("dry-run publish %s as %s", asset.Dest, asset.Name)
		}
		return nil
	}
	release, err := ensureRelease(cfg, token)
	if err != nil {
		return err
	}
	for _, asset := range assets {
		if err := uploadAsset(repoRoot, token, release.UploadURL, asset); err != nil {
			return err
		}
	}
	return nil
}

// selectedGroups returns the set of asset IDs to operate on. When the caller passes
// no groups (or "all"), every distinct asset ID declared in gnb.toml is selected so
// adding a new [[paks.assets]] entry is automatically picked up by `gnb setup` and
// `gnb paks fetch` without further code changes.
func selectedGroups(cfg config.Config, groups []string) map[string]bool {
	if len(groups) == 0 || contains(groups, "all") {
		selected := map[string]bool{}
		for _, asset := range cfg.Paks.Assets {
			selected[strings.ToLower(asset.ID)] = true
		}
		return selected
	}
	selected := map[string]bool{}
	for _, group := range groups {
		group = strings.ToLower(group)
		selected[group] = true
		// Backward compatibility: the old MagicaLego audio group was published as
		// separate sfx files. It now lives in assets/paks/magicalego.pak.
		if group == "sfx" {
			selected["magicalego"] = true
		}
	}
	return selected
}

func contains(values []string, needle string) bool {
	for _, value := range values {
		if strings.EqualFold(value, needle) {
			return true
		}
	}
	return false
}

type releaseInfo struct {
	ID        int64  `json:"id"`
	UploadURL string `json:"upload_url"`
}

func ensureRelease(cfg config.Config, token string) (releaseInfo, error) {
	url := fmt.Sprintf("https://api.github.com/repos/%s/releases/tags/%s", cfg.Paks.Repo, cfg.Paks.ReleaseTag)
	req, _ := http.NewRequest(http.MethodGet, url, nil)
	req.Header.Set("Authorization", "Bearer "+token)
	req.Header.Set("Accept", "application/vnd.github+json")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return releaseInfo{}, err
	}
	defer resp.Body.Close()
	if resp.StatusCode == http.StatusOK {
		var release releaseInfo
		return release, json.NewDecoder(resp.Body).Decode(&release)
	}
	if resp.StatusCode != http.StatusNotFound {
		body, _ := io.ReadAll(resp.Body)
		return releaseInfo{}, fmt.Errorf("GitHub release lookup failed: %s %s", resp.Status, body)
	}

	body, _ := json.Marshal(map[string]any{
		"tag_name":    cfg.Paks.ReleaseTag,
		"name":        "Optional Assets (" + cfg.Paks.ReleaseTag + ")",
		"body":        "Optional binary assets consumed by gnb paks fetch.",
		"draft":       false,
		"prerelease":  false,
		"make_latest": "false",
	})
	req, _ = http.NewRequest(http.MethodPost, fmt.Sprintf("https://api.github.com/repos/%s/releases", cfg.Paks.Repo), bytes.NewReader(body))
	req.Header.Set("Authorization", "Bearer "+token)
	req.Header.Set("Accept", "application/vnd.github+json")
	req.Header.Set("Content-Type", "application/json")
	resp, err = http.DefaultClient.Do(req)
	if err != nil {
		return releaseInfo{}, err
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		data, _ := io.ReadAll(resp.Body)
		return releaseInfo{}, fmt.Errorf("GitHub release create failed: %s %s", resp.Status, data)
	}
	var release releaseInfo
	return release, json.NewDecoder(resp.Body).Decode(&release)
}

func uploadAsset(repoRoot string, token string, uploadURL string, asset config.PakAsset) error {
	uploadURL = strings.Split(uploadURL, "{")[0] + "?name=" + asset.Name
	path := filepath.Join(repoRoot, asset.Dest)
	file, err := os.Open(path)
	if err != nil {
		return err
	}
	defer file.Close()

	info, err := file.Stat()
	if err != nil {
		return err
	}

	req, _ := http.NewRequest(http.MethodPost, uploadURL, file)
	req.ContentLength = info.Size()
	req.Header.Set("Authorization", "Bearer "+token)
	req.Header.Set("Content-Type", "application/octet-stream")
	req.Header.Set("Accept", "application/vnd.github+json")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		data, _ := io.ReadAll(resp.Body)
		return fmt.Errorf("upload failed for %s: %s %s", asset.Name, resp.Status, data)
	}
	console.Success("uploaded %s", asset.Name)
	return nil
}
