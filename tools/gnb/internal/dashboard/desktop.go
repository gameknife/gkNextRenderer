package dashboard

import (
	"context"
	"errors"
	"fmt"
	"sync"

	"github.com/wailsapp/wails/v2"
	"github.com/wailsapp/wails/v2/pkg/options"
	"github.com/wailsapp/wails/v2/pkg/options/assetserver"
	"github.com/wailsapp/wails/v2/pkg/options/windows"
	wailsruntime "github.com/wailsapp/wails/v2/pkg/runtime"
)

// RunDesktop hosts regular dashboard requests in Wails and starts a loopback
// server for responses that require incremental flushing, such as job logs and
// streaming chat. Wails' custom asset transport buffers response bodies.
func (s *Server) RunDesktop(ctx context.Context) error {
	streamCtx, stopStream := context.WithCancel(ctx)
	running, err := s.start(streamCtx, 0)
	if err != nil {
		stopStream()
		return err
	}
	s.streamBaseURL = running.URL
	fmt.Printf("dashboard running in Wails desktop window (streams: %s)\n", running.URL)

	appDone := make(chan struct{})
	var appDoneOnce sync.Once
	closeAppDone := func() {
		appDoneOnce.Do(func() { close(appDone) })
	}

	wailsErr := wails.Run(&options.App{
		Title:            "gnb Dashboard",
		Width:            1360,
		Height:           840,
		MinWidth:         960,
		MinHeight:        640,
		Frameless:        true,
		WindowStartState: options.Normal,
		BackgroundColour: &options.RGBA{R: 21, G: 22, B: 23, A: 255},
		AssetServer: &assetserver.Options{
			Handler: s.routes(),
		},
		OnStartup: func(appCtx context.Context) {
			go func() {
				select {
				case <-ctx.Done():
					wailsruntime.Quit(appCtx)
				case <-appDone:
				}
			}()
		},
		OnDomReady: func(appCtx context.Context) {
			wailsruntime.WindowExecJS(appCtx,
				`document.documentElement.classList.add("wails-desktop");`)
		},
		OnShutdown: func(context.Context) {
			closeAppDone()
		},
		Windows: &windows.Options{
			Theme:                             windows.Dark,
			ResizeDebounceMS:                  8,
			DisableFramelessWindowDecorations: true,
		},
	})

	closeAppDone()
	stopStream()
	return errors.Join(wailsErr, running.Wait())
}
