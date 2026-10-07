# Remaster plan status

One line per task of [REMASTER_PLAN.md](REMASTER_PLAN.md). States: `todo`, `in progress`, `done`, `blocked`.

## Phase 1

* P1.1 - done - d4f0b17d3
* P1.2 - done - d4f0b17d3
* P1.3 - done
* P1.4 - todo
* P1.5 - todo
* P1.6 - todo

## Notes

* P1.3 design change: the scalers take the 8-bit indexed frame plus the 256-entry palette instead of an RGBA
  copy (`fheroes2::PaletteScaler` in `src/engine/image_scaler.h`). Colour distances become a 256 x 256 table
  lookup, which is several times faster and avoids one full-frame conversion per render.
* P1.3 design change: the Sharp mode is a separable Catmull-Rom bicubic upscale followed by a clamped 5-tap
  sharpen, not an edge-direction estimating filter. It is the fallback for non-integer scales; xBRZ is far
  better at integer scales.
* P1.3 measurements (Release, 4 cores, 1280x720 source, full frame): xBRZ 2x 8.8 ms, xBRZ 3x 9.6 ms,
  Sharp 1.5x (to 1920x1080) 26 ms, Sharp 2x 50 ms, Sharp 3x 96 ms. 50 random sub-rectangles per mode match
  the full-frame result exactly (`scaler_test` tool).

* Headless verification runs the game under `xvfb-run` with `XDG_CONFIG_HOME` pointing to a scratch
  directory and captures the root window with ImageMagick `import`.
