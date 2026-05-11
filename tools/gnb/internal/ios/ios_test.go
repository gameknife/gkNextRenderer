package ios

import (
	"reflect"
	"testing"
)

func TestConfigureArgs(t *testing.T) {
	tests := []struct {
		name         string
		skipCodeSign bool
		want         []string
	}{
		{
			name:         "skip codesign",
			skipCodeSign: true,
			want:         []string{"--preset", "ios", "-DIOS_SKIP_CODE_SIGN=ON"},
		},
		{
			name:         "enable codesign",
			skipCodeSign: false,
			want:         []string{"--preset", "ios", "-DIOS_SKIP_CODE_SIGN=OFF"},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := configureArgs(tt.skipCodeSign); !reflect.DeepEqual(got, tt.want) {
				t.Fatalf("configureArgs(%v) = %v, want %v", tt.skipCodeSign, got, tt.want)
			}
		})
	}
}

func TestBuildArgs(t *testing.T) {
	want := []string{"--build", "--preset", "ios", "--target", "gkNextRenderer"}
	if got := buildArgs("gkNextRenderer"); !reflect.DeepEqual(got, want) {
		t.Fatalf("buildArgs() = %v, want %v", got, want)
	}
}
