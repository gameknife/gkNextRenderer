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

const (
	dashboardMinWidth  = 1280
	dashboardMinHeight = 800
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
		Width:            dashboardMinWidth,
		Height:           dashboardMinHeight,
		MinWidth:         dashboardMinWidth,
		MinHeight:        dashboardMinHeight,
		Frameless:        true,
		StartHidden:      true,
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
			width, height := dashboardWindowSize(appCtx)
			wailsruntime.WindowSetSize(appCtx, width, height)
			wailsruntime.WindowCenter(appCtx)
			wailsruntime.WindowShow(appCtx)
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

func dashboardWindowSize(ctx context.Context) (int, int) {
	screens, err := wailsruntime.ScreenGetAll(ctx)
	if err != nil {
		fmt.Printf("warning: failed to get screen size: %v\n", err)
		return dashboardMinWidth, dashboardMinHeight
	}

	return dashboardWindowSizeForScreens(screens)
}

func dashboardWindowSizeForScreens(screens []wailsruntime.Screen) (int, int) {
	var selected *wailsruntime.Screen
	for i := range screens {
		if screens[i].IsCurrent {
			selected = &screens[i]
			break
		}
	}
	if selected == nil {
		for i := range screens {
			if screens[i].IsPrimary {
				selected = &screens[i]
				break
			}
		}
	}
	if selected == nil && len(screens) > 0 {
		selected = &screens[0]
	}
	if selected == nil {
		return dashboardMinWidth, dashboardMinHeight
	}

	screenWidth, screenHeight := selected.Size.Width, selected.Size.Height
	if screenWidth <= 0 {
		screenWidth = selected.Width
	}
	if screenHeight <= 0 {
		screenHeight = selected.Height
	}

	return max(screenWidth*7/10, dashboardMinWidth),
		max(screenHeight*7/10, dashboardMinHeight)
}
