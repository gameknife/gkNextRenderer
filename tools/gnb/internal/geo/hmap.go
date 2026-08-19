package geo

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math"
)

// HeightGrid is a regular sampled heightfield in SCAD-local metres.
// Row 0 is the -y (south) edge; x is the fastest axis.
type HeightGrid struct {
	Cols, Rows   int
	OriginX      float64 // SCAD position of sample (0, 0)
	OriginY      float64
	CellX, CellY float64
	Values       []float64
}

// NewHeightGrid allocates a zeroed grid covering [originX, originX+(cols-1)*cellX].
func NewHeightGrid(cols, rows int, originX, originY, cellX, cellY float64) *HeightGrid {
	return &HeightGrid{
		Cols: cols, Rows: rows,
		OriginX: originX, OriginY: originY,
		CellX: cellX, CellY: cellY,
		Values: make([]float64, cols*rows),
	}
}

// At returns the sample at integer grid coordinates, clamped to the border.
func (g *HeightGrid) At(col, row int) float64 {
	if col < 0 {
		col = 0
	} else if col >= g.Cols {
		col = g.Cols - 1
	}
	if row < 0 {
		row = 0
	} else if row >= g.Rows {
		row = g.Rows - 1
	}
	return g.Values[row*g.Cols+col]
}

// Set writes a sample (no-op outside the grid).
func (g *HeightGrid) Set(col, row int, v float64) {
	if col < 0 || col >= g.Cols || row < 0 || row >= g.Rows {
		return
	}
	g.Values[row*g.Cols+col] = v
}

// Sample bilinearly interpolates at SCAD-space (x, y), clamping at the border.
// Mirrors FHeightGrid::Sample in the engine so the generator and the runtime
// agree about what the file means.
func (g *HeightGrid) Sample(x, y float64) float64 {
	if g.Cols < 1 || g.Rows < 1 {
		return 0
	}
	gx := clampF((x-g.OriginX)/g.CellX, 0, float64(g.Cols-1))
	gy := clampF((y-g.OriginY)/g.CellY, 0, float64(g.Rows-1))
	x0, y0 := int(gx), int(gy)
	tx, ty := gx-float64(x0), gy-float64(y0)
	h00, h10 := g.At(x0, y0), g.At(x0+1, y0)
	h01, h11 := g.At(x0, y0+1), g.At(x0+1, y0+1)
	return (h00*(1-tx)+h10*tx)*(1-ty) + (h01*(1-tx)+h11*tx)*ty
}

// Bounds returns the min and max sample.
func (g *HeightGrid) Bounds() (lo, hi float64) {
	if len(g.Values) == 0 {
		return 0, 0
	}
	lo, hi = g.Values[0], g.Values[0]
	for _, v := range g.Values {
		if v < lo {
			lo = v
		}
		if v > hi {
			hi = v
		}
	}
	return lo, hi
}

// PosX / PosY give the SCAD coordinate of a sample.
func (g *HeightGrid) PosX(col int) float64 { return g.OriginX + float64(col)*g.CellX }
func (g *HeightGrid) PosY(row int) float64 { return g.OriginY + float64(row)*g.CellY }

const hmapMagic = "GKHM"

// EncodeHmap serialises the grid into the .hmap side-car format consumed by
// the engine's ["hmap", ...] terrain operator (see FScadTerrain.h).
//
// The quantisation scale is chosen from the value range so the int16 payload
// never clips: 1cm resolution when the range allows it, coarser for very tall
// tiles.
func EncodeHmap(g *HeightGrid) ([]byte, error) {
	if g.Cols < 2 || g.Rows < 2 {
		return nil, fmt.Errorf("hmap needs at least 2x2 samples, got %dx%d", g.Cols, g.Rows)
	}
	if len(g.Values) != g.Cols*g.Rows {
		return nil, fmt.Errorf("hmap value count %d does not match %dx%d", len(g.Values), g.Cols, g.Rows)
	}
	lo, hi := g.Bounds()
	bias := (lo + hi) / 2
	span := math.Max(math.Abs(hi-bias), math.Abs(lo-bias))
	scale := 0.01
	if span/scale > 32000 {
		scale = span / 32000
	}

	buf := new(bytes.Buffer)
	buf.WriteString(hmapMagic)
	write := func(v any) {
		_ = binary.Write(buf, binary.LittleEndian, v)
	}
	write(uint32(1))
	write(uint32(g.Cols))
	write(uint32(g.Rows))
	write(float32(g.OriginX))
	write(float32(g.OriginY))
	write(float32(g.CellX))
	write(float32(g.CellY))
	write(float32(scale))
	write(float32(bias))
	for _, v := range g.Values {
		raw := math.Round((v - bias) / scale)
		if raw > 32767 {
			raw = 32767
		} else if raw < -32768 {
			raw = -32768
		}
		write(int16(raw))
	}
	return buf.Bytes(), nil
}

// DecodeHmap reads a .hmap blob back. Used by the round-trip test and by
// anything that wants to inspect a committed file.
func DecodeHmap(data []byte) (*HeightGrid, error) {
	const headerSize = 40
	if len(data) < headerSize || string(data[:4]) != hmapMagic {
		return nil, fmt.Errorf("not a .hmap blob")
	}
	r := bytes.NewReader(data[4:])
	var version, cols, rows uint32
	var originX, originY, cellX, cellY, scale, bias float32
	for _, p := range []any{&version, &cols, &rows, &originX, &originY, &cellX, &cellY, &scale, &bias} {
		if err := binary.Read(r, binary.LittleEndian, p); err != nil {
			return nil, err
		}
	}
	if version != 1 {
		return nil, fmt.Errorf("unsupported .hmap version %d", version)
	}
	need := headerSize + int(cols)*int(rows)*2
	if len(data) < need {
		return nil, fmt.Errorf("truncated .hmap: have %d bytes, need %d", len(data), need)
	}
	g := NewHeightGrid(int(cols), int(rows), float64(originX), float64(originY),
		float64(cellX), float64(cellY))
	for i := range g.Values {
		raw := int16(binary.LittleEndian.Uint16(data[headerSize+i*2:]))
		g.Values[i] = float64(raw)*float64(scale) + float64(bias)
	}
	return g, nil
}

func clampF(v, lo, hi float64) float64 {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}
