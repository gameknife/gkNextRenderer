package spec

import (
	"fmt"
	"path/filepath"
)

const (
	dirName     = ".spec"
	todoFile    = "TODO.md"
	archiveFile = "ARCHIVE.md"
	journalDir  = "journal"
	blockerDir  = "blockers"
	specDir     = "specs"
)

func Root(repoRoot string) string         { return filepath.Join(repoRoot, dirName) }
func TODOPath(repoRoot string) string     { return filepath.Join(Root(repoRoot), todoFile) }
func ArchivePath(repoRoot string) string  { return filepath.Join(Root(repoRoot), archiveFile) }
func JournalDir(repoRoot string) string   { return filepath.Join(Root(repoRoot), journalDir) }
func BlockerDir(repoRoot string) string   { return filepath.Join(Root(repoRoot), blockerDir) }
func SpecsDir(repoRoot string) string     { return filepath.Join(Root(repoRoot), specDir) }
func JournalPath(repoRoot string, id int) string {
	return filepath.Join(JournalDir(repoRoot), fmt.Sprintf("%05d.md", id))
}
func BlockerPath(repoRoot string, id int) string {
	return filepath.Join(BlockerDir(repoRoot), fmt.Sprintf("%05d.md", id))
}
func SpecPath(repoRoot string, id int) string {
	return filepath.Join(SpecsDir(repoRoot), fmt.Sprintf("%05d.md", id))
}

// JournalRel/BlockerRel/SpecRel return the relative reference used inside TODO.md.
func JournalRel(id int) string { return fmt.Sprintf("journal/%05d.md", id) }
func BlockerRel(id int) string { return fmt.Sprintf("blockers/%05d.md", id) }
func SpecRel(id int) string    { return fmt.Sprintf("specs/%05d.md", id) }
