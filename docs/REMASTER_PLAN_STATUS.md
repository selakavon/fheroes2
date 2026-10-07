# Remaster plan status

One line per task of [REMASTER_PLAN.md](REMASTER_PLAN.md). States: `todo`, `in progress`, `done`, `blocked`.

## Phase 1

* P1.1 - done - d4f0b17d3
* P1.2 - done - d4f0b17d3
* P1.3 - done
* P1.4 - done
* P1.5 - done
* P1.6 - done

## Phase 2

* P2.1 - done - 2f259867d
* P2.2 - done
* P2.3 - done
* P2.4 - done
* P2.5 - todo
* P2.6 - todo

## Phase 1 exit criteria

* Nearest mode screenshot-identical to `master`: yes (0 differing pixels, 1280x720 main menu under Xvfb).
* No seams or ghosting with partial redraws: verified by opening and closing dialogs and sub-menus under xBRZ.
* Full-frame performance: the row-diff optimisation (P1.4.5) is implemented. Measured in Release on the
  adventure map (960x540 game, 2x xBRZ to 1920x1080, 4 cores): full-frame updates avg 8.8 ms, median 6.0 ms,
  max 20 ms; partial updates avg 2.4 ms. Sharp: full-frame avg 20 ms, median 11 ms; partial avg 7 ms.
* CI: not run locally. Local checks passed: clang-format diff of the whole branch is empty, copyright headers
  valid, markdownlint clean, Debug and Release builds have no warnings in the touched files.

## Notes

* P2.1 design change: `_gameOptions` has only five unused bits left, so the six visual effect flags live in a separate
  `BitModes _fxOptions` member of `Settings`. The config keys are as planned (`fx day night`, ... `fx post processing
  preset`). The Visual Effects dialog uses the empty option icon background with a short text (Sun, Leaf, Rain, Shade,
  UI, FX) as icons until dedicated icons are drawn. The Graphics dialog second row became a three-option row (V-Sync,
  System Info, Visual Effects). Post-Processing shows "N/A" and is not clickable unless the game is built with
  `ENABLE_GPU_RENDERER` (P2.6).
* P2.2: the tint is applied by `Display::subscribeRGBPalette()`; `Display::_renderFrame()` remembers the last colour
  cycling index remap and re-pushes it with the tinted RGB palette, so water keeps animating in phase while tinted
  (verified: two night screenshots one second apart differ only in the water). Custom palettes set through
  `Display::changePalette()` by fades and videos are left alone; the tint returns on the first frame after the default
  palette is restored (a small pop at the end of fade-ins, accepted). The 600 ms transition is driven by
  `RenderProcessor::registerFullFrameRenderRequester()` polled from `LocalEvent::HandleEvents()`.
* P2.2 dialog handling: `fheroes2::StandardWindow` and `Dialog::ResizableFrameBox` (all standard message boxes) report
  themselves to `Ambience`, so the tint is removed while such a dialog is open and restored when it closes. Full-screen
  game screens that are not dialogs (castle, hero, puzzle, kingdom overview) stay tinted. The battlefield window is
  itself a `StandardWindow` and is excluded with `Ambience::rebaseDialogDepth()`. Verified headlessly: day 1 to day 8
  screenshots show dawn, morning, noon, afternoon, dusk, evening, night and dawn again (side panel mean colour goes
  125,90,61 at noon to 50,45,48 at night); with both tints off the main menu is pixel-identical to `master`; the end
  turn confirmation dialog renders untinted at night. Battle tint was not verified headlessly.

* P2.3 design changes: the fog noise mask is a seamless 256x256 tile (generated with ImageMagick, source PNG under
  `files/images/weather/`) instead of 64x64 because a 64 px period tiled visibly; mirrored 64 px variants looked like a
  kaleidoscope. The fog is not blitted as a sprite: at first use the mask is converted into per-row runs of pixels
  that share a whitening level, and the engine's transform tables 8 (light) and 7 (dense) are sampled through
  `ApplyTransform()` on a 256-entry ramp so that the runs can be applied directly to the frame (table 6 flattens
  everything to one gray and is not used). Near-black palette entries are left untouched so the unexplored part of
  the map stays black. Particles are updated on a dedicated 50 ms `WEATHER_DELAY` tick rather than the 250 ms
  `MAPS_DELAY`, which is too coarse for rain. The weather type is chosen in `Ambience::weatherType()` from a hash of
  the map seed and the week number, with 10% fog added to spring, autumn and winter. Leaves are 5x4 pixel sprites
  (3 shapes, 2 frames) and read as small drifting specks at native resolution.
* P2.3 h2d packing: `h2dmgr combine` merges into an existing target file, so the new images were added on top of the
  committed `resurrection.h2d`. Re-combining from extracted PNGs loses the transform (shadow) layers of 25 existing
  sprites, so never rebuild the file that way. The new entries are also listed in `h2d.cpp`, which validates the file
  list at start-up.
* P2.3 verification (headless, 960x540 game area scaled to 1920x1080, each type forced through a temporary override):
  rain streaks, snow dots, leaves and fog banks are visible on the map and animate between frames; the end turn
  dialog renders clean on top of them. Release draw cost on the 768x508 game area: rain 0.10 ms, fog 0.44 ms at
  27% mask coverage (about 0.85 ms extrapolated to a 1280x720 game area). The weather is drawn only from the
  Adventure Map redraw with `LEVEL_WEATHER`, so other `GameArea::Redraw()` users (editor, puzzle) are unaffected;
  with the option off `Weather::draw()` returns before touching the frame.

* P2.4: `fheroes2::softenShadow()` (engine `image.cpp`) works on the transform layer only: shadow pixels (values 2
  to 5) that touch a transparent pixel become one level lighter and transparent pixels that touch the shadow get a
  shadow two levels lighter than their darkest shadow neighbour, so the usual level 2 shadow becomes core 2, edge 3,
  halo 4. The image is not enlarged, and pixels on the image border are treated as if the image continued, so
  objects split into 32x32 tile sprites keep continuous shadows (a 1 px notch where a shadow outline crosses a tile
  seam is possible). It is applied in `loadICN()` right after `processICN()` to the Adventure Map object ICNs listed
  by `MP2::getIcnIdFromObjectIcnType()`, the hero, boat and monster shadow sprites (`SHADOW32`, `BOATSHAD`,
  `MINI_MONSTER_SHADOW`) and every battle unit ICN from `getMonsterData()`, so heroes and monsters get the same
  softened shadow rather than a newly generated one. The original assets already contain some level 3 to 5 pixels;
  these are shifted the same way, capped at 5.
* P2.4 toggle: `Assets::resetSoftShadowImages()` clears the cached sprites of all affected ICNs (plus
  `MINI_MONSTER_IMAGE`, which is generated together with its shadow) when the option is switched in the Visual
  Effects dialog, and the lazy loader rebuilds them with the new setting. Verified headlessly: toggling the option
  from the in-game System Options > Graphics > Visual Effects dialog changes only the shadow outlines of the visible
  map (13k pixels on a 1920x1080 frame) and the config file is updated; palm tree shadows show the lighter edge and
  halo at 6x zoom. With the option off the loader does not touch the sprites. Battle shadows were not verified
  headlessly.

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
