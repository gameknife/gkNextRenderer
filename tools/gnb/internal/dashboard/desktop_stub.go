//go:build !desktop

package dashboard

import (
	"context"
	"fmt"
)

// RunDesktop falls back to browser-compatible mode when the binary was built
// without Wails desktop support.
func (s *Server) RunDesktop(ctx context.Context) error {
	fmt.Println("dashboard desktop support is unavailable in this gnb binary; using browser mode")
	return s.Run(ctx)
}
