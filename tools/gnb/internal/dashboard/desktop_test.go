package dashboard

import (
	"testing"

	wailsruntime "github.com/wailsapp/wails/v2/pkg/runtime"
)

func TestDashboardWindowSizeForScreens(t *testing.T) {
	tests := []struct {
		name       string
		screens    []wailsruntime.Screen
		wantWidth  int
		wantHeight int
	}{
		{
			name:       "no screens uses minimum",
			wantWidth:  dashboardMinWidth,
			wantHeight: dashboardMinHeight,
		},
		{
			name: "common desktop uses seventy percent width",
			screens: []wailsruntime.Screen{
				testScreen(1920, 1080, true, false),
			},
			wantWidth:  1344,
			wantHeight: dashboardMinHeight,
		},
		{
			name: "large desktop uses seventy percent size",
			screens: []wailsruntime.Screen{
				testScreen(3840, 2160, true, false),
			},
			wantWidth:  2688,
			wantHeight: 1512,
		},
		{
			name: "current screen takes precedence over primary",
			screens: []wailsruntime.Screen{
				testScreen(3840, 2160, true, false),
				testScreen(5120, 1440, false, true),
			},
			wantWidth:  3584,
			wantHeight: 1008,
		},
		{
			name: "deprecated dimensions remain a fallback",
			screens: []wailsruntime.Screen{
				{IsPrimary: true, Width: 3200, Height: 1800},
			},
			wantWidth:  2240,
			wantHeight: 1260,
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			width, height := dashboardWindowSizeForScreens(test.screens)
			if width != test.wantWidth || height != test.wantHeight {
				t.Fatalf("dashboardWindowSizeForScreens() = %dx%d, want %dx%d",
					width, height, test.wantWidth, test.wantHeight)
			}
		})
	}
}

func testScreen(width, height int, primary, current bool) wailsruntime.Screen {
	screen := wailsruntime.Screen{
		IsPrimary: primary,
		IsCurrent: current,
	}
	screen.Size.Width = width
	screen.Size.Height = height
	return screen
}
