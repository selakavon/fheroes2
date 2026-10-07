# Remaster plan status

One line per task of [REMASTER_PLAN.md](REMASTER_PLAN.md). States: `todo`, `in progress`, `done`, `blocked`.

## Phase 1

* P1.1 - done - d4f0b17d3
* P1.2 - done - d4f0b17d3
* P1.3 - done
* P1.4 - done
* P1.5 - done
* P1.6 - done

## Phase 1 exit criteria

* Nearest mode screenshot-identical to `master`: yes (0 differing pixels, 1280x720 main menu under Xvfb).
* No seams or ghosting with partial redraws: verified by opening and closing dialogs and sub-menus under xBRZ.
* Full-frame performance: the row-diff optimisation (P1.4.5) is implemented. Measured in Release on the
  adventure map (960x540 game, 2x xBRZ to 1920x1080, 4 cores): full-frame updates avg 8.8 ms, median 6.0 ms,
  max 20 ms; partial updates avg 2.4 ms. Sharp: full-frame avg 20 ms, median 11 ms; partial avg 7 ms.
* CI: not run locally. Local checks passed: clang-format diff of the whole branch is empty, copyright headers
  valid, markdownlint clean, Debug and Release builds have no warnings in the touched files.

## Notes

* P1.5: the resolution dialog already labelled scaled entries as "960 x 540 (x2.0)", so only the resolution
  list generation was extended (2x, 3x, 4x of the biggest screen resolution, even sizes, near-duplicates of
  existing entries skipped). Verified on a 2560x1440 virtual screen: "840 x 525 (x2.0)"-style entries appear
  next to the exact ones. Switching the scaling type at runtime through the dialog re-creates the renderer
  correctly for all four modes.
* In-game FPS under Xvfb (software SDL renderer) is identical for nearest, xBRZ and Sharp on the adventure
  map, so the scaler is not the limiting factor there. It is not a GPU-backed measurement.

* P1.4: the CPU scaler path keeps the SDL logical size equal to the game resolution (mouse coordinates are
  unaffected) and only enlarges the streaming texture. xBRZ uses the smallest integer factor not below the
  window scale (2 to 4) and lets SDL shrink the supersampled texture linearly when the window scale is not
  an integer. Verified headlessly under Xvfb: nearest mode is pixel-identical to master (0 differing pixels),
  xBRZ and Sharp render correctly at 960x540 scaled to 1920x1080, opening and closing dialogs leaves no seams.

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
