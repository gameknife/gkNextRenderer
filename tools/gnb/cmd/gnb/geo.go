package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/geo"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/spf13/cobra"
)

// geoFlags is shared by every subcommand so `fetch`/`build`/`scad`/`make` all
// describe the same tile.
type geoFlags struct {
	name     string
	at       string
	size     float64
	cells    int
	profile  string
	seed     int
	debug    bool
	endpoint string
}

func (f *geoFlags) bind(cmd *cobra.Command) {
	cmd.Flags().StringVar(&f.name, "name", "", "tile identifier (cache dir + output file name)")
	cmd.Flags().StringVar(&f.at, "at", "", "tile centre as lat,lon (e.g. 22.2855,114.1580)")
	cmd.Flags().Float64Var(&f.size, "size", 1000, "tile side length in metres")
	cmd.Flags().IntVar(&f.cells, "cells", geo.DefaultCells, "terrain grid cells per axis (max 176)")
	cmd.Flags().StringVar(&f.profile, "profile", "default", "building height profile (default, china, hongkong)")
	cmd.Flags().IntVar(&f.seed, "seed", 7, "terrain jitter seed")
	cmd.Flags().BoolVar(&f.debug, "debug-images", false, "write per-stage DEM greyscale PNGs into the cache")
	cmd.Flags().StringVar(&f.endpoint, "overpass-endpoint", geo.OverpassEndpoint,
		"Overpass API mirror (switch when the default one keeps refusing the tile)")
}

func (f *geoFlags) tile() (geo.Tile, error) {
	lat, lon, err := parseLatLon(f.at)
	if err != nil {
		return geo.Tile{}, err
	}
	t := geo.Tile{
		Name: f.name, Lat: lat, Lon: lon, SizeM: f.size,
		Cells: f.cells, Profile: f.profile, Seed: f.seed,
	}
	return t, t.Normalize()
}

func parseLatLon(s string) (float64, float64, error) {
	parts := strings.Split(strings.TrimSpace(s), ",")
	if len(parts) != 2 {
		return 0, 0, fmt.Errorf("--at must be lat,lon (got %q)", s)
	}
	lat, err := strconv.ParseFloat(strings.TrimSpace(parts[0]), 64)
	if err != nil {
		return 0, 0, fmt.Errorf("--at latitude: %w", err)
	}
	lon, err := strconv.ParseFloat(strings.TrimSpace(parts[1]), 64)
	if err != nil {
		return 0, 0, fmt.Errorf("--at longitude: %w", err)
	}
	return lat, lon, nil
}

func newGeoCommand(ctx appContext) *cobra.Command {
	cmd := &cobra.Command{
		Use:   "geo",
		Short: "Generate a .scad city level from public elevation + OpenStreetMap data",
		Long: "Five staged, individually re-runnable steps (see\n" +
			"docs/designs/geo-city-generation-design.md):\n" +
			"  fetch  SRTM .hgt + Overpass JSON -> external/geocache/<tile>/\n" +
			"  build  -> normalised IR + assets/scad/geo/<tile>/terrain.hmap\n" +
			"  scad   -> assets/scad/proc/generated/<tile>.scad\n" +
			"  make   all of the above\n\n" +
			"Raw downloads and the IR are ODbL-derived databases and stay out of the\n" +
			"repository; the generated .scad and .hmap carry the required attribution.",
	}
	cmd.AddCommand(newGeoFetchCommand(ctx))
	cmd.AddCommand(newGeoBuildCommand(ctx))
	cmd.AddCommand(newGeoScadCommand(ctx))
	cmd.AddCommand(newGeoMakeCommand(ctx))
	return cmd
}

func geoOptions(ctx appContext, f geoFlags) geo.Options {
	opt := geo.DefaultOptions(ctx.repoRoot)
	opt.DebugImages = f.debug
	opt.Warnf = geoWarn
	if f.endpoint != "" {
		opt.OverpassEndpoint = f.endpoint
	}
	return opt
}

// console.Info is itself Printf-style: pre-formatting here would make any '%'
// in the message (a percentage in a report line) re-enter the formatter.
func geoLog(format string, args ...any) { console.Info(format, args...) }

func geoWarn(format string, args ...any) { console.Warn(format, args...) }

func newGeoFetchCommand(ctx appContext) *cobra.Command {
	var f geoFlags
	cmd := &cobra.Command{
		Use:   "fetch",
		Short: "Download the raw DEM and OSM data for a tile (cached)",
		RunE: func(cmd *cobra.Command, args []string) error {
			tile, err := f.tile()
			if err != nil {
				return err
			}
			meta, err := geo.Fetch(tile, geoOptions(ctx, f), geoLog)
			if err != nil {
				return err
			}
			for _, s := range meta.Sources {
				console.Label(s.Kind, fmt.Sprintf("%s (%d KB)", s.Path, s.Bytes/1024))
			}
			return nil
		},
	}
	f.bind(cmd)
	return cmd
}

func newGeoBuildCommand(ctx appContext) *cobra.Command {
	var f geoFlags
	cmd := &cobra.Command{
		Use:   "build",
		Short: "Normalise the cached data into the IR and write terrain.hmap",
		RunE: func(cmd *cobra.Command, args []string) error {
			tile, err := f.tile()
			if err != nil {
				return err
			}
			_, err = geo.Build(tile, geoOptions(ctx, f), geoLog)
			return err
		},
	}
	f.bind(cmd)
	return cmd
}

func newGeoScadCommand(ctx appContext) *cobra.Command {
	var f geoFlags
	cmd := &cobra.Command{
		Use:   "scad",
		Short: "Emit the .scad scene from the cached IR + .hmap",
		RunE: func(cmd *cobra.Command, args []string) error {
			tile, err := f.tile()
			if err != nil {
				return err
			}
			return geoEmit(ctx, tile, geoOptions(ctx, f))
		},
	}
	f.bind(cmd)
	return cmd
}

func newGeoMakeCommand(ctx appContext) *cobra.Command {
	var f geoFlags
	cmd := &cobra.Command{
		Use:   "make",
		Short: "fetch + build + scad in one go",
		Example: "  gnb geo make --name hk_victoria --at 22.2855,114.1580 --size 1000 " +
			"--profile hongkong",
		RunE: func(cmd *cobra.Command, args []string) error {
			tile, err := f.tile()
			if err != nil {
				return err
			}
			opt := geoOptions(ctx, f)
			if _, err := geo.Fetch(tile, opt, geoLog); err != nil {
				return err
			}
			if _, err := geo.Build(tile, opt, geoLog); err != nil {
				return err
			}
			return geoEmit(ctx, tile, opt)
		},
	}
	f.bind(cmd)
	return cmd
}

// geoEmit renders the scene and mirrors both the scene and its .hmap side-car
// into the build assets, so `gnb shot` sees them without a rebuild.
func geoEmit(ctx appContext, tile geo.Tile, opt geo.Options) error {
	scadPath, _, err := geo.Scad(tile, opt, geoLog)
	if err != nil {
		return err
	}
	paths := geo.NewPaths(ctx.repoRoot, tile.Name)
	for _, src := range []string{scadPath, paths.HmapPath(), paths.POIPath(), paths.AttributionPath()} {
		rel, relErr := filepath.Rel(filepath.Join(ctx.repoRoot, "assets"), src)
		if relErr != nil || filepath.IsAbs(rel) || isDotDot(rel) {
			continue
		}
		dst := filepath.Join(filepath.Dir(platform.BinDir(ctx.repoRoot, ctx.preset)), "assets", rel)
		if err := copyFileTo(src, dst); err != nil {
			console.Warn("build-assets mirror failed: " + err.Error())
		}
	}
	rel, err := filepath.Rel(ctx.repoRoot, scadPath)
	if err != nil {
		rel = scadPath
	}
	console.Info("wrote: " + filepath.ToSlash(rel))
	console.Info("preview: gnb shot --scene " + filepath.ToSlash(rel))
	if info, err := os.Stat(scadPath); err == nil {
		console.Label("scene", fmt.Sprintf("%d KB", info.Size()/1024))
	}
	return nil
}
