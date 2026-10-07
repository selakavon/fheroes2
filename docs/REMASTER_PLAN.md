# fheroes2 Remaster Plan

This document is an implementation plan for a visual remaster of fheroes2. It is written so that an
autonomous coding agent (Claude Code) can execute it phase by phase, task by task, without additional
design decisions. Every task names the files to touch, the exact behaviour to implement, and how to
verify it.

Read the whole document once before starting any task. Then execute tasks strictly in order inside a
phase. Do not start a phase before the previous phase's exit criteria are met.

---

## 0. Ground rules for the implementing agent

### 0.1 How the engine renders today (read this first)

* Every `fheroes2::Image` (`src/engine/image.h`) stores **two byte layers** in one buffer: layer 0 is a
  256-colour palette index per pixel, layer 1 is a "transform" byte per pixel (0 = opaque pixel,
  1 = fully transparent, 2..15 = darkening/lightening table applied to whatever is underneath).
  `_singleLayer` images have only layer 0.

* The whole frame is composed on the CPU into `fheroes2::Display` (a subclass of `Image`,
  `src/engine/screen.h`). Rendering to the OS window happens in `Display::render()` →
  `Display::_renderFrame()` → `BaseRenderEngine::render()`.

* The SDL2 backend is the `RenderEngine` class in `src/engine/screen.cpp` (second one in the file,
  around line 845, deriving from `BaseSDLRenderer`). It keeps an `SDL_Surface` and one `SDL_Texture`.
  If the SDL driver supports `SDL_PIXELFORMAT_INDEX8` the surface is 8-bit and `Display` writes directly
  into it (`linkRenderSurface`). Otherwise the surface is 32-bit and `copyImageToSurface()` resolves
  indices through `_palette32Bit`.

* The final stretch to the window is done by SDL via `SDL_RenderSetLogicalSize` plus the
  `SDL_HINT_RENDER_SCALE_QUALITY` hint (nearest or linear). There are no shaders anywhere.

* `ResolutionInfo` (`src/engine/screen.h`) already distinguishes `gameWidth/gameHeight` (logical frame)
  from `screenWidth/screenHeight` (window). `FilterResolutions()` in `screen.cpp` already generates
  integer-scaled entries (e.g. game 960x540 on a 1920x1080 window).

* Water/lava animation ("colour cycling") is done by rotating palette entries:
  `RenderProcessor::preRenderAction()` (`src/engine/render_processor.cpp`) returns a new 256-entry
  palette, `Display::_renderFrame()` calls `_engine->updatePalette()` and re-renders the full frame.

* Palette effects (`src/engine/pal.h`, `PaletteType`) are 256-byte index→index remap tables applied with
  `fheroes2::ApplyPalette()`. Team colours, darkening, grayscale, evil interface all work this way.

* Original art comes from `HEROES2.agg` as ICN files, decoded and cached in
  `src/fheroes2/game/game_assets.cpp` (`_icnVsSprite`, `Assets::getImage( icnId, index )`). Project-added art
  lives in `files/data/resurrection.h2d` (`src/engine/h2d_file.h`, `src/fheroes2/h2d/h2d.cpp`), created with
  the `h2dmgr` tool in `src/tools/`.

* UI geometry is in logical pixels. Adventure map tiles are 32 px (`tileWidthPx` in
  `src/fheroes2/gui/ui_constants.h`). Default/minimum logical resolution is 640x480.

* Settings are in `src/fheroes2/system/settings.{h,cpp}` (bit flags in `_gameOptions`, text config file
  `fheroes2.cfg` parsed with `TinyConfig`). The graphics options UI is
  `src/fheroes2/dialog/dialog_graphics_settings.cpp`.

* There is no unit test suite in the repository. Verification is manual plus CI builds
  (`.github/workflows/`: cmake, make, msvc, android, ios, clang-tidy, iwyu, code style).

### 0.2 Conventions

* C++17, follow `.clang-format` (run `clang-format -i` on every touched file). Follow
  `docs/DEVELOPMENT.md` and `CONTRIBUTING.md`.

* Every new user-facing string goes through `_( "..." )`.
* Every new behaviour must be **opt-in** and default to the current look. When all new options are off,
  the rendered frame must be identical to current `master`.

* Platforms that must keep building and running: Linux, Windows (MSVC + MinGW), macOS, Android, iOS,
  Emscripten, PS Vita, Nintendo Switch. Anything using GPU shaders is desktop-only and behind a CMake
  option that defaults to OFF.

* Build and run between tasks:

  ```bash
  cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_TOOLS=ON
  cmake --build build -j
  ./build/src/fheroes2/fheroes2
  ```

  The game needs the original `HEROES2.AGG` data in `data/` (or the demo, fetched with
  `script/demo/download_demo_version.sh`).

* Commit after each task with a message of the form `Remaster P<phase>.<task>: <summary>`.
* If a task cannot be completed as specified, stop and write the reason into
  `docs/REMASTER_PLAN_STATUS.md` instead of silently changing the design.

### 0.3 Status tracking

Maintain `docs/REMASTER_PLAN_STATUS.md` with one line per task: `P1.3 - done - <commit hash>` or
`P1.3 - blocked - <reason>`. Update it as the last step of every task.

---

## Phase 1 — Smart upscaling and internal render scale

**Goal.** A visibly sharper, cleaner image on high-DPI screens with zero asset changes. The game renders
at a lower internal ("game") resolution and is upscaled by a pixel-art-aware scaler on the CPU. This is
the closest 2D equivalent to DLSS-style reconstruction and works on every platform.

**End state.** A "Screen Scaling Type" option with four values: Nearest, Linear, xBRZ, Sharp. xBRZ and
Sharp operate on a 32-bit copy of the frame and upload a window-sized texture. Nearest and Linear
behave exactly as today.

### P1.1 — Replace the boolean scaling flag with an enum

Files: `src/engine/screen.h`, `src/engine/screen.cpp`, `src/fheroes2/system/settings.h`,
`src/fheroes2/system/settings.cpp`.

1. In `src/engine/screen.h` add, inside namespace `fheroes2`:

   ```cpp
   enum class ScalingType : int
   {
       NEAREST = 0,
       LINEAR = 1,
       XBRZ = 2,
       SHARP = 3
   };
   ```

2. In `BaseRenderEngine` replace `isNearestScaling()` / `setNearestScaling( bool )` with
   `ScalingType scalingType() const` and `void setScalingType( ScalingType )` (store in a
   `ScalingType _scalingType{ ScalingType::NEAREST }` member). Keep `isNearestScaling()` as an inline
   helper returning `_scalingType == ScalingType::NEAREST` so existing call sites keep compiling.
3. In `settings.cpp`:
   * Keep the config key `screen scaling type` but accept `nearest`, `linear`, `xbrz`, `sharp` and write
     the same strings back (line ~360 for reading, ~559 for writing). Unknown values map to `nearest`.
   * Replace the `GAME_SCREEN_SCALING_TYPE_NEAREST` bit flag usage with a `fheroes2::ScalingType
     _scalingType` member in `Settings`. Keep the enum value in the flags list reserved (do not reuse the
     bit) to avoid breaking saved configs.
   * Replace `setScreenScalingTypeNearest( bool )` / `isScreenScalingTypeNearest()` with
     `setScreenScalingType( fheroes2::ScalingType )` / `getScreenScalingType()`; the setter forwards to
     `fheroes2::engine().setScalingType()` exactly like the current code does at line ~906.
4. Fix all call sites (`grep -rn "ScalingTypeNearest\|NearestScaling" src/`).

Verify: build; toggle the option in the graphics dialog; confirm `fheroes2.cfg` round-trips the four
strings; confirm `nearest` and `linear` render identically to before.

### P1.2 — Graphics settings dialog: four-way option

File: `src/fheroes2/dialog/dialog_graphics_settings.cpp`.

1. In the `drawOptions` lambda (around line 128) draw the option text as `Nearest`, `Linear`, `xBRZ` or
   `Sharp` depending on `conf.getScreenScalingType()`. Reuse icon `ICN::GAME_OPTION_ICON` index 3 for
   Nearest, 2 for Linear, and index 2 for xBRZ/Sharp until Phase 2 adds dedicated icons.
2. On left click (`case SelectedWindow::ScreenScalingType`, around line 266) cycle
   NEAREST → LINEAR → XBRZ → SHARP → NEAREST.
3. Replace the help text at line ~208 with:
   "Nearest keeps every pixel sharp and looks best at integer scales. Linear smooths the image but makes
   it blurry. xBRZ reconstructs smooth edges from the pixel art and is the recommended option on large
   screens. Sharp applies an edge-preserving upscale with sharpening."
4. After changing the type call `fheroes2::Display::instance().render()` with a full-screen ROI so the
   change is visible immediately.

Verify: all four values selectable with mouse and keyboard; help popup shows the new text.

### P1.3 — Add the xBRZ scaler to the engine

Files: new `src/engine/image_scaler.h`, new `src/engine/image_scaler.cpp`,
`src/engine/CMakeLists.txt`, `Makefile`-based builds (`src/engine/Makefile`), the MSVC project
`src/engine/engine-vs2019.vcxproj` and filters, Android/iOS/Emscripten build lists if they enumerate
source files (grep for `image_tool.cpp` to find every list that must be extended).

1. Implement xBRZ **from scratch** in C++17 (do not vendor the original xBRZ code; it is GPL-3 and the
   project is GPL-2+; a clean-room implementation of the published algorithm is fine). Scope:
   * Input: `const uint32_t * src` (RGBA8888, row pitch in pixels), `int32_t width`, `int32_t height`.
   * Output: `uint32_t * dst` at integer scale factor 2, 3 or 4, pitch `width * scale`.
   * Support a sub-rectangle: `scaleRegion( src, srcPitch, roi, dst, dstPitch, scale )` where the scaler
     reads a 2-pixel border around `roi` (clamped to the image) and writes only `roi * scale`.
   * Colour distance in YCbCr with the standard xBRZ luminance weight; equal-colour tolerance constant
     exposed as a parameter (default 30).
   * Blending rules for the 3x3 kernel with the 4 corner cases (the classic xBRZ "blend info"
     dominance test), for scales 2, 3 and 4. 5x and 6x are not required.
   * Treat alpha as part of the colour (the Display frame is always opaque, alpha is 255).
2. Public API in `image_scaler.h`:

   ```cpp
   namespace fheroes2
   {
       enum class ScalerKind { XBRZ, SHARP };
       // dst must hold (width*scale) x (height*scale) pixels.
       void scaleRegion( ScalerKind kind, const uint32_t * src, int32_t srcPitch, int32_t width, int32_t height,
                         const Rect & roi, uint32_t * dst, int32_t dstPitch, int32_t scale );
   }
   ```

3. Implement `SHARP` as a two-pass filter: bilinear/bicubic-style edge-adaptive upscale (EASU-like:
   12-tap, direction and length estimation from the 3x3 luminance neighbourhood) followed by a
   contrast-adaptive sharpen (RCAS-like, 5-tap cross, sharpness 0.8, clamped to the local min/max). Do
   not copy AMD's FSR source; implement the method. SHARP must support **non-integer** scales, since it
   is used when the window is not an integer multiple of the game resolution.
4. Add a `std::vector<std::thread>`-based row-band parallel driver inside `image_scaler.cpp`: split the
   ROI into `std::thread::hardware_concurrency()` horizontal bands (minimum band height 32 rows),
   scale each band in its own thread, join. Keep a static thread count cap of 8. Do not use the existing
   `AsyncManager` (`src/engine/thread.h`); it is a single-worker helper for audio.

Verify: write a throwaway program under `src/tools/` guarded by `ENABLE_TOOLS` (`scaler_test.cpp`) that
loads a PNG via SDL_image when `ENABLE_IMAGE` is on, scales it 2x/3x/4x with both kinds and writes the
result as PNG. Inspect the output: xBRZ must give smooth curved edges on sprites without blurring flat
areas; SHARP must not ring more than 1 pixel around hard edges. Remove or keep the tool; if kept, add it
to the tools README.

### P1.4 — 32-bit upscaled render path in the SDL2 backend

File: `src/engine/screen.cpp` (SDL2 `RenderEngine`, ~line 845 onwards).

1. Add members: `SDL_Texture * _scaledTexture{ nullptr }`, `std::vector<uint32_t> _frame32`,
   `std::vector<uint32_t> _scaled32`, `int32_t _scale{ 1 }`, `bool _useCpuScaler{ false }`.
2. In `allocate()`:
   * After computing `resolutionInfo`, set `_useCpuScaler = ( scalingType() == XBRZ || scalingType() == SHARP )`.
   * `_scale` = largest integer `s` in {2,3,4} such that `gameWidth * s <= screenWidth && gameHeight * s <= screenHeight`;
     if no such `s` exists, `_scale = 1` and force `_useCpuScaler = false` (fall back to the current path
     with the `linear` hint).
   * For XBRZ: if `gameWidth * _scale != screenWidth` or `gameHeight * _scale != screenHeight`, keep
     `_scale` and let SDL do the remaining non-integer stretch with the `linear` hint (document this in a
     comment; it is the standard "integer xBRZ then bilinear" approach).
   * For SHARP: scale directly to `screenWidth x screenHeight` (non-integer allowed), `_scale` is unused.
   * When `_useCpuScaler`, **do not** request the INDEX8 driver: create the `_surface` as 32-bit so that
     `copyImageToSurface()` resolves the palette and `Display` does not link its buffer to the surface
     (`linkRenderSurface` must only be called in the 8-bit path, check line ~1373). Create
     `_scaledTexture` with `SDL_CreateTexture( _renderer, SDL_PIXELFORMAT_RGBA32,
     SDL_TEXTUREACCESS_STREAMING, outW, outH )` where `outW/outH` is the scaled size. Still call
     `SDL_RenderSetLogicalSize( _renderer, outW, outH )`.
3. In `render()`:
   * Existing path untouched when `!_useCpuScaler`.
   * When `_useCpuScaler`: after `copyImageToSurface()`, compute `roi2 = roi` expanded by 2 px on each side
     and clamped to the frame; call `scaleRegion()` from `_surface->pixels` into `_scaled32` (pitch
     `outW`); then `SDL_UpdateTexture( _scaledTexture, &scaledArea, ... )` for `scaledArea = roi2 * scale`
     (full texture when `roi` is the full frame); `SDL_RenderCopy( _renderer, _scaledTexture, ... )`;
     present.
   * Handle the surface pixel format: `copyImageToSurface()` already fills `_palette32Bit` in the surface's
     own byte order (`getRGBA/getBGRA/...`). The scaler is byte-order agnostic as long as it treats the
     pixel as 4 equal channels for distance; make the YCbCr conversion use the surface's `SDL_PixelFormat`
     shifts (pass `Rmask/Gmask/Bmask` shift values to the scaler, or convert once to RGBA32 before scaling;
     choose the conversion, simplicity wins).
4. In `clear()` destroy `_scaledTexture`. In `setScalingType()` on the engine, if the mode switches
   between CPU-scaler and non-CPU-scaler, call `Display::instance().resetRenderer()` semantics
   (re-`allocate()` with the current resolution) so the texture set-up is rebuilt. Look at how
   `setVSync` triggers `_toggleVSync` and follow the same pattern.
5. Palette cycling: `Display::_renderFrame()` already forces a full-frame render after
   `updatePalette()` in the 32-bit path. That means a full-frame rescale ~4.5 times per second on the
   adventure map. Measure it (see verify). If a full-frame xBRZ 3x at 1280x720 game resolution exceeds
   8 ms on a 4-core machine, implement the optimisation: keep the previous `_frame32`, compare rows of
   the new frame to the old one, and only rescale the changed rows (water tiles change, everything else
   does not).

Verify:

* Set `videomode = 960x540` style scaled entry (choose a resolution entry whose screen size is an integer
  multiple of the game size from the Resolution dialog), set `screen scaling type = xbrz`; the main menu
  and adventure map must render with smooth edges and no seams at ROI borders when moving the cursor
  (ROI test: hover buttons, open and close dialogs, scroll the map).

* `screen scaling type = nearest` must be pixel-identical to `master` (take a screenshot with each build
  and `compare` them with ImageMagick: zero differing pixels).

* Add a temporary `DEBUG_LOG` of the scale time per frame; report the numbers in
  `REMASTER_PLAN_STATUS.md` for 2x/3x/4x on your machine.

### P1.5 — Internal resolution ("render scale") in the Resolution dialog

Files: `src/engine/screen.cpp` (`FilterResolutions`), `src/fheroes2/dialog/dialog_resolution.cpp`.

1. `FilterResolutions()` already adds integer-scaled entries only when the hardware resolution is
   divisible. Extend it: for the largest hardware resolution, add entries with game size equal to
   `screen / s` for `s` in {2, 3, 4} **rounded down to even numbers** whenever `screen / s >= 640x480`,
   even if the division is not exact (SHARP handles non-integer, xBRZ falls back to integer + linear).
   Deduplicate against existing entries.
2. In the resolution list UI, label scaled entries as `960 x 540 (2x → 1920 x 1080)` style strings. Find
   the formatting code in `dialog_resolution.cpp` (search for `gameWidth`), and append the scale suffix
   when `screenWidth != gameWidth`.
3. Default behaviour unchanged: first run still picks the current default.

Verify: on a 1920x1080 display, the list contains 960x540 (2x) and 640x480 (3x) entries; selecting one
with xBRZ active gives a crisp 1080p picture with 2x/3x-sized UI.

### P1.6 — Documentation and defaults

Files: `docs/README.md` (graphics section, if any), `docs/FAQ.md`, `fheroes2.cfg` template generation in
`settings.cpp` (`String()` function around line 559).

1. Document the four scaling types and the render-scale resolutions.
2. Add the four accepted values to the comment emitted into `fheroes2.cfg`.
3. Keep `nearest` as default.

### Phase 1 exit criteria

* All CI workflows green (including Android, iOS, Emscripten, MSVC).
* Nearest mode screenshot-identical to `master`.
* xBRZ 3x full-frame at 1280x720 game resolution under 8 ms on a 4-core desktop, or the row-diff
  optimisation from P1.4.5 implemented.

* No visible seams or ghosting when only partial ROIs are redrawn.

---

## Phase 2 — Ambience and a new presentation layer

**Goal.** Make the game _feel_ new: dynamic lighting mood, weather, an optional modern UI skin and,
on desktop, GPU post-processing. The engine stays 8-bit indexed.

**End state.** A new "Visual Effects" page in the graphics settings with independent toggles:
Day/Night Cycle, Seasonal Tint, Weather, Enhanced Shadows, UI Skin (Classic / Modern) and, when built
with `ENABLE_GPU_RENDERER`, a Post-Processing toggle with Bloom, Vignette, CRT presets. All default off.

### P2.1 — Settings and dialog scaffolding

Files: `settings.{h,cpp}`, `dialog_graphics_settings.cpp`, new
`src/fheroes2/dialog/dialog_visual_effects.{h,cpp}`.

1. Add bit flags in `Settings::_gameOptions` (pick unused bits; check the full list at the top of
   `settings.cpp`): `GAME_FX_DAY_NIGHT`, `GAME_FX_SEASON_TINT`, `GAME_FX_WEATHER`,
   `GAME_FX_ENHANCED_SHADOWS`, `GAME_FX_MODERN_UI`, `GAME_FX_POSTPROCESS`. Add an `int _postProcessPreset`
   (0 none, 1 bloom, 2 vignette, 3 crt). Config keys: `fx day night`, `fx season tint`, `fx weather`,
   `fx enhanced shadows`, `fx modern ui`, `fx post processing` (values `on`/`off`), `fx post processing
   preset` (`bloom`/`vignette`/`crt`).
2. Add a sixth option "Visual Effects" to the configuration window in `dialog_graphics_settings.cpp`
   (follow the pattern of `windowVSyncRoi`, add a `SelectedWindow::VisualEffects` case) that opens the
   new dialog. Lay out the new dialog like `dialog_graphics_settings.cpp` (same `drawOption` helper, a
   two-row grid of options and an Okay button).
3. Every toggle must take effect immediately on return to the game screen (`Interface::AdventureMap::redraw`
   with full flags, or `Display::render()` full ROI).

Verify: toggles persist in `fheroes2.cfg`; nothing changes visually yet.

### P2.2 — Day/night and seasonal palette tints

Files: `src/engine/pal.{h,cpp}`, `src/engine/render_processor.{h,cpp}`, `src/engine/screen.{h,cpp}`,
`src/fheroes2/world/world.h` (day counter accessor), `src/fheroes2/game/game_startgame.cpp` or wherever
`RenderProcessor::instance().startColorCycling()` is called.

Design: tints are applied to the **256-entry RGB palette that reaches the renderer**, not to pixels, so
they cost nothing per pixel and compose with colour cycling. The pre-render hook already produces a
256-byte index remap; extend the pipeline so it can also produce an RGB palette.

1. In `screen.h` add `Display::PreRenderProcessing` overload semantics: keep the existing
   `std::function<bool( std::vector<uint8_t> & )>` and add a second callback type
   `std::function<bool( std::vector<RGB> & /_rgbPalette_/ )>` stored as `_rgbPreprocessing`. In
   `_renderFrame()`, after the index remap is applied, call `_rgbPreprocessing` with a copy of the
   current RGB palette (see `changePalette()` / `_createPalette()` in the backends) and, if it returns
   true, push the modified RGB palette with `changePalette( data, true )`. Mark a full-frame render.
2. In `pal.cpp` add a pure function
   `std::vector<RGB> tintPalette( const std::vector<RGB> & in, const float r, const float g, const float b, const float brightness, const float saturation )`
   using per-channel multiply, a brightness multiply and an HSL-free saturation lerp toward luma. Clamp
   to 0..255. **Never** modify entries 0 (transparent key) or the 16 cycling indices listed in
   `docs/GRAPHICAL_ASSETS.md` in a way that breaks cycling: apply the same tint to them (tint is
   uniform, so cycling still animates).
3. Add `src/fheroes2/gui/ambience.{h,cpp}` with a singleton `Ambience` that:
   * Receives the current in-game day (`world.CountDay()`), week and month.
   * Computes `TimeOfDay` as a float in [0,1) from the day counter when `GAME_FX_DAY_NIGHT` is on:
     day 1 = dawn, cycle length 7 days (one week = one full cycle, so each day of the week has a distinct
     lighting: dawn, morning, noon, afternoon, dusk, evening, night).
   * Maps time of day to tint parameters: noon = identity; dusk = (1.05, 0.9, 0.75, 0.95, 1.0);
     night = (0.6, 0.7, 1.0, 0.7, 0.8); dawn = (1.0, 0.9, 0.85, 0.9, 1.0); interpolate linearly.
   * Seasonal tint when `GAME_FX_SEASON_TINT` is on: month 1..12 → spring (1.0,1.03,0.97),
     summer identity, autumn (1.05,0.97,0.85), winter (0.92,0.96,1.05, brightness 1.05, saturation 0.85).
     Multiply with the day/night tint.
   * Exposes `bool apply( std::vector<RGB> & palette )` which returns false when both toggles are off or
     the game is not on the adventure map or battle screen.
4. Register the callback where colour cycling is registered. The tint must apply only on the adventure
   map and in battle, **never** in menus or dialogs rendered on top (dialogs are drawn into the same
   frame, so instead: disable the tint while a modal dialog is open by having `Ambience` check a
   "dialog depth" counter incremented/decremented by the standard dialog box constructor/destructor,
   `fheroes2::StandardWindow` in `src/fheroes2/gui/ui_window.cpp`). Acceptable simplification: tint
   stays active under dialogs; document the choice in the status file if chosen.
5. Smooth transitions: when the day advances (end turn), interpolate from the previous tint to the new
   one over 600 ms using the render processor's timer, requesting full-frame renders at ~30 fps during
   the transition only.

Verify: enable Day/Night, end turns, watch the map go dusk → night → dawn over a week; disable → exact
original colours (screenshot compare). Water still animates while tinted. Battle inherits the tint.

### P2.3 — Weather overlays

Files: new `src/fheroes2/gui/weather.{h,cpp}`, `src/fheroes2/gui/interface_gamearea.cpp`
(`GameArea::Redraw`), `files/data/resurrection.h2d` (new sprites), `src/fheroes2/agg/icn.h`
(new ICN ids), `game_assets.cpp` (loader for the new h2d sprites; follow how existing h2d-backed ICNs
are loaded: search `readImageFromH2D`/`h2d::readImage` usage).

1. Weather types: NONE, RAIN, SNOW, FOG, LEAVES. Selected deterministically per week from the map seed
   and the season (`Ambience` owns the choice; winter → snow 50%, autumn → leaves 30%/rain 30%,
   spring → rain 40%, summer → none 80%/rain 20%).
2. Particle layer: a fixed-size pool (max 600 particles) in **logical pixels**, each with position,
   velocity, size (1 or 2 px) and a palette colour index (white 10/11 for snow, light blue for rain,
   browns for leaves). Rendered with `fheroes2::SetPixel`/`DrawLine` into the game area image **after**
   `LEVEL_FOG` and before the cursor, clipped to the game area ROI. Fog: a semi-transparent sprite
   layer using transform values 2..4 scrolled slowly (use `ApplyTransform` over 64x64 tiles with a
   precomputed noise mask stored in the h2d as a single-layer sprite).
3. Animation driven from the existing game-area animation tick (search `Game::validateAnimationDelay`
   with `Game::MAPS_DELAY` in `interface_adventure_map.cpp`); update particles once per tick and
   request a game-area redraw via `setRedraw( REDRAW_GAMEAREA )`.
4. Performance: particles only exist inside the visible ROI plus 32 px margin; no per-frame allocation.
5. Author the fog noise mask and 3 leaf sprites (2 frames each) as PNGs under `files/images/weather/`
   (keep sources in the repo) and pack them into `resurrection.h2d` with `h2dmgr combine`. Keep the
   PNGs palette-restricted to `KB.PAL` colours (use `pal2img` to view the palette).

Verify: enable Weather, play through weeks; rain/snow visible on the map, not on dialogs; frame time
on the adventure map increases by less than 1 ms at 1280x720; disabled → identical to original.

### P2.4 — Enhanced shadows

Files: `src/engine/image.cpp` (`addGradientShadow`, `addShadow`, `makeShadow`, `updateShadow`),
`game_assets.cpp` where object sprites are post-processed.

1. When `GAME_FX_ENHANCED_SHADOWS` is on, at ICN load time, for adventure map object ICNs (trees,
   mountains, buildings; list them by walking `ICN::` ids used by `Maps::Tiles` object rendering), replace
   the hard 1-level transform shadow (transform id 2 or 3) with a 3-step gradient: outer ring transform 4,
   middle 3, core 2, generated by dilating/eroding the existing shadow mask by 1 px. Implement as
   `fheroes2::softenShadow( Sprite & )` in `image.cpp`.
2. Add a soft drop shadow to heroes and monsters on the adventure map (`makeShadow` with a 2-step
   gradient) and to battle units (`battle_interface.cpp` draws unit shadows; locate `ICN::` shadow
   sprites or the transform-based shadow and reuse `softenShadow`).
3. Because the asset cache is populated lazily, the toggle requires a cache flush: call the existing
   asset reset (search for how `Assets` are invalidated when switching evil/good interface; there is a
   function that clears `_icnVsSprite` entries) when the toggle changes.

Verify: side-by-side screenshots of the same map with and without the option; shadows have visible
soft edges; disabled → identical to original.

### P2.5 — Modern UI skin

Files: `files/images/ui_modern/` (new source PNGs), `resurrection.h2d`, `game_assets.cpp`,
`src/fheroes2/gui/ui_button.cpp`, `ui_window.cpp`, `ui_text.cpp`, fonts in `game_assets.cpp`
(`_normalFont`, `_smallFont` generation around line 400).

1. Skin = a replacement set for: window border (`ICN::SURDRBKE`/`SURDRBKG` family), standard buttons
   (`ICN::BUTTON_*`), the adventure map side panel background, scrollbars, and the two fonts. All at
   the same logical pixel sizes as the originals (no layout changes in this phase).
2. Art direction: flat-shaded, higher contrast, rounded 2 px corners, a consistent 4-colour metal
   gradient, fonts re-hinted with cleaner 1 px strokes. All colours must be from `KB.PAL`
   (palette-restricted), avoid the 16 cycling indices.
3. Loader: when `GAME_FX_MODERN_UI` is on, `Assets::getImage()` resolves the ICN from the h2d override
   set first (`ICN` id → h2d name map in a new `src/fheroes2/game/game_assets_modern.cpp`), otherwise
   falls back to the original. Flush the cache on toggle (same mechanism as P2.4).
4. Ship the skin partially: start with window border, buttons and fonts. Record missing elements in
   the status file.

Verify: toggle on → menus and dialogs use the new border/buttons/fonts, everything stays aligned and
clickable; toggle off → original assets.

### P2.6 — Optional GPU post-processing backend (desktop only)

Files: `CMakeLists.txt` (`option(ENABLE_GPU_RENDERER ... OFF)`), new
`src/engine/screen_gl.cpp` compiled only with the option, `screen.cpp` (`RenderEngine::create()` picks
the GL engine when the option is compiled in and `GAME_FX_POSTPROCESS` is on), new
`src/engine/shaders/*.glsl` embedded as string literals via a generated header.

1. Backend: SDL2 window with `SDL_WINDOW_OPENGL`, OpenGL ES 2.0 / OpenGL 2.1 compatible GLSL 100
   shaders (one implementation for Linux, Windows, macOS; do not target mobile). Load GL functions via
   SDL's `SDL_GL_GetProcAddress` with a tiny hand-written loader for the ~25 functions needed (no GLEW
   dependency).
2. Pipeline: upload the 32-bit resolved frame (`copyImageToSurface` into a client buffer) as an RGBA
   texture at game resolution → pass 1: upscale shader (nearest, or GPU port of SHARP: EASU+RCAS as two
   fragment shaders, which supersedes the CPU scaler for this backend) to an FBO at window size →
   pass 2: post-process preset shader (bloom = 2-pass separable Gaussian on a threshold-bright copy,
   additive 0.25; vignette = radial darkening 0.35 at corners; CRT = scanlines at window rows,
   0.15 darkening, slight barrel 0.02, aperture mask) → default framebuffer.
3. Partial ROI uploads via `glTexSubImage2D`; always re-run passes on the full window.
4. Fall back automatically to the SDL2 engine if context creation fails; log it.
5. Cursor: the GL engine must support the software cursor path (`Cursor::enableSoftwareEmulation`) and
   hardware cursor identically to the SDL2 engine; reuse `RenderCursor` code.

Verify: build with `-DENABLE_GPU_RENDERER=ON` on Linux and Windows; toggle presets at runtime;
screenshot with Post-Processing off and the GL backend active equals the SDL2 Nearest screenshot.

### Phase 2 exit criteria

* All effects individually toggleable and default off; all-off screenshot equals `master`.
* CI green on every platform with `ENABLE_GPU_RENDERER` OFF; a desktop CI job builds it ON.
* Adventure map frame time with Day/Night + Weather + Enhanced Shadows on is within 2 ms of baseline
  at 1280x720.

---

## Phase 3 — HD asset pack and true-colour engine

**Goal.** Real 2x artwork across the game. This requires a true-colour image path in the engine and an
asset scale factor, plus a user-run tool that produces the HD pack from the user's own AGG files
(the original art is owned by Ubisoft and cannot be redistributed).

**End state.** `fheroes2 --hd` (and a settings toggle "HD Assets" shown only when an HD pack is present)
runs the full game with 2x sprites, true-colour rendering, all palette effects reproduced as colour
operations, and automatic fallback to xBRZ-upscaled originals for any sprite missing from the pack.

Do this on a long-lived branch `remaster/hd`. Rebase on `master` weekly.

### P3.1 — True-colour `Image` format

Files: `src/engine/image.{h,cpp}`, `src/engine/image_tool.{h,cpp}`, `src/engine/screen.{h,cpp}`.

1. Add `enum class PixelFormat : uint8_t { INDEXED8, RGBA32 }` to `Image`. Storage stays one
   `std::unique_ptr<uint8_t[]> _data`; for RGBA32 the layer 0 is `width*height*4` bytes and the transform
   layer (layer 1, 1 byte per pixel) is kept with **identical semantics** (0 opaque, 1 transparent,
   2..15 shading table). Add `PixelFormat format() const`, `int32_t bytesPerPixel() const`,
   `uint8_t * image()` keeps returning the raw layer 0 pointer, add `uint32_t * image32()` /
   `const uint32_t * image32() const` with asserts on format.
2. `Image::resize`, `reset`, `copy`, move/copy constructors, `_updateRoi` handle both formats.
   `Sprite` and `Display` inherit automatically. `Display` gets a format chosen at `setResolution` time
   (`RGBA32` when HD mode is active).
3. For every free function in `image.h` (full list: `addGradientShadow`, `addGradientShadowForArea`,
   `addShadow`, `AlphaBlit` x3, `ApplyPalette` x6, `ApplyAlpha`, `ApplyTransform`, `Blit` x5, `Copy` x3,
   `copyTransformLayer` x2, `CreateContour`, `CreateDitheringTransition`, `Crop`, `DrawBorder`,
   `DrawLine`, `DrawRect`, `DivideImageBySquares`, `ExtractCommonPattern`, `Fill`, `FillTransform`,
   `FilterOnePixelNoise`, `FitToRoi`, `Flip` x2, `GetActiveROI`, `GetColorId`, `makeShadow`,
   `ReplaceColorId`, `ReplaceColorIdByTransformId`, `ReplaceTransformIdByColorId`, `Resize` x2,
   `SetPixel` x2, `SetTransformPixel`, `Stretch`, `SubpixelResize` x2, `Transpose`, `updateShadow`)
   implement an RGBA32 branch. Rules:
   * Mixed-format operations: `INDEXED8 → RGBA32` blits convert on the fly through the current standard
     palette (`PAL::GetPalette( STANDARD )` + `fheroes2::getGamePalette()`); `RGBA32 → INDEXED8` is an
     error (`assert`), never needed.
   * Transform-layer shading (`2..15`) on RGBA32: precompute 14 multiplier tables matching the
     brightness of the indexed shading tables (measure the average luminance ratio each table produces
     on the standard palette and store the 14 ratios as constants in `image.cpp`).
   * `ApplyPalette( RGBA32 )` with a `PaletteType` uses a per-type **colour transform** instead of a
     remap table: implement `PAL::transformColor( PaletteType, RGBA ) → RGBA` in `pal.cpp`:
     `GRAY` = luma; `DARKENING` = x0.5; `RED`/`BROWN`/`TAN`/`PURPLE`/font colours = luma mapped through a
     3-stop gradient sampled from the corresponding indexed palette (build the gradient at start-up by
     applying the indexed table to a grey ramp and reading the resulting RGB); `GOOD_TO_EVIL_*` = HSV hue
     rotation + saturation that best matches the indexed result (fit once, store constants);
     `MIRROR_IMAGE`/`NO_CYCLE` = identity on colour, keep transform semantics; `CUSTOM` tables
     (e.g. team-colour remaps built with `GetColorId`) are handled in P3.3.
   * `Fill`, `DrawLine`, `SetPixel`, `DrawRect`, `DrawBorder` keep their `uint8_t colorId` signatures and
     look the colour up in the standard palette when drawing into RGBA32 images; add RGBA overloads for
     new code.
4. Backends: `copyImageToSurface()` gains a straight `memcpy`/row copy path when the display is RGBA32.
   The CPU scalers from Phase 1 and the GL backend from Phase 2 already consume 32-bit frames.
5. Testing: since the repository has no unit tests, add a minimal self-check executable under
   `src/tools/image_selftest.cpp` (built with `ENABLE_TOOLS`) that, for each function above, runs the
   indexed version and the RGBA version on the same random images (RGBA created by converting the
   indexed one) and reports max per-channel difference; the budget is ≤ 2 for shading tables and 0 for
   copies/blits/flips. Run it in the `cmake.yml` workflow.

Verify: game runs unchanged in indexed mode (default). With a debug env var `FHEROES2_FORCE_RGBA=1`
the display is RGBA32 and all original (indexed) assets render correctly through on-the-fly conversion.
Screenshot compare against indexed mode: max per-channel difference ≤ 2.

### P3.2 — Colour cycling in true colour

Files: `src/engine/render_processor.{h,cpp}`, `src/engine/pal.cpp`, `game_assets.cpp`,
`src/fheroes2/maps/maps_tiles_render.cpp` (or wherever water/lava tiles are drawn).

Palette rotation cannot animate RGBA pixels. Replace it in HD mode by **pre-baked animation frames**:

1. The cycling index sets are 214..217, 218..221, 231..235 and 238..241 (see `PAL::GetCyclingPalette()` in
   `src/engine/pal.cpp`). Three sets have period 4, the 231..235 set has period 5, so the full palette
   period is 20 steps.
2. At HD-pack build time (P3.5) and at fallback load time: for each sprite, determine which cycling sets
   it uses and compute its period as the least common multiple of their lengths (1, 4, 5 or 20). Generate
   one frame per step by applying `GetCyclingPalette( step )` to the indexed source **before** upscaling,
   and store the frames as `name#cycleN`. Sprites without cycling pixels have one frame.
3. `Assets::getImage()` in HD mode takes the current cycling step from `RenderProcessor`
   (`_cyclingCounter`, make it readable) and returns the matching frame. The render processor keeps
   its timer, but in HD mode `preRenderAction` returns false (no palette push) and instead sets a
   "cycling step changed" flag that the adventure map/battle/castle screens consume to request a redraw
   of the game area only (not the whole frame).

Verify: water, lava and the flame/glow effects animate in HD mode at the same rate as in indexed mode.

### P3.3 — Team colours and custom remaps

Files: `game_assets.cpp` (search for `PAL::PaletteType::CUSTOM` and `GetColorId`/`ReplaceColorId` usages:
hero flags, castle flags, monster "owner" tints, mini-map colours), `src/fheroes2/kingdom/`,
`src/fheroes2/gui/interface_radar.cpp`.

1. Every place that builds a custom remap from colour ids must also work in RGBA. Implement
   `fheroes2::recolorMask( Image & rgba, const Image & indexedSource, const std::vector<uint8_t> & remap )`:
   it uses the **indexed source sprite** as the mask (pixels whose index is in the remap's domain) and
   replaces the corresponding RGBA pixel's hue/saturation with the target colour's while keeping its
   luminance. In HD mode the indexed source is still available (the AGG is loaded), scaled with nearest
   2x to obtain the mask.
2. Mini-map and radar keep using indexed colour ids drawn into RGBA via the standard palette lookup.

Verify: flags for all 6 players are correct on the map and in castle/hero dialogs; radar colours intact.

### P3.4 — Asset scale factor

Files: `src/fheroes2/gui/ui_constants.h`, `src/engine/screen.{h,cpp}`, `game_assets.cpp`, and
callers that mix logical and pixel coordinates.

Design: **logical coordinates stay unchanged**. HD mode introduces `fheroes2::assetScale()` returning 1
or 2. The `Display` is allocated at `gameWidth * assetScale x gameHeight * assetScale` physical pixels,
and _all_ drawing code keeps working in logical units by routing through a scaled coordinate layer.

Choose the simplest viable mechanism and implement it exactly:

1. Introduce `fheroes2::Display::logicalWidth()/logicalHeight()` (= physical / assetScale). Audit
   every call of `display.width()` / `display.height()` in `src/fheroes2` (several hundred); replace them
   with `logicalWidth()/logicalHeight()`. This is mechanical; do it with a scripted rename and then fix
   the few sites in `src/engine` that really need the physical size (the render backends).
2. Make the drawing primitives scale-aware **at the `Image` free-function level**: `Blit`, `Copy`,
   `AlphaBlit`, `Fill`, `Crop`, `Resize`, `DrawLine`, etc. accept logical coordinates and, when the
   destination image is flagged `_isScaledSurface` (true only for `Display` and images explicitly created
   as scaled surfaces such as the game area back buffer), multiply positions/sizes by `assetScale()`.
   Sprites loaded from the HD pack are already 2x in pixels and carry their offsets `x()/y()` in logical
   units multiplied back out, so the maths is: `dstX_phys = outX * s`, source rectangles already in
   physical pixels.
3. Images created by game code as scratch buffers (`fheroes2::Image temp( w, h )`) keep logical
   semantics unless they are blitted from scaled sprites; audit sites with `grep -rn "fheroes2::Image [a-z]*( " src/fheroes2`
   and convert those that compose sprites to `fheroes2::ScaledImage` (a thin subclass setting the flag
   and allocating `w*s x h*s`).
4. Mouse input is already in logical window coordinates via `SDL_RenderSetLogicalSize`; no change.
5. Text: fonts in the HD pack are 2x bitmaps; `ui_text.cpp` measures in logical units by dividing glyph
   widths by `assetScale()`. Line heights and kerning constants there must be multiplied.
6. Tile rendering: `tileWidthPx` stays 32 (logical). The game area back buffer becomes a
   `ScaledImage`.

Milestone ordering for bring-up (do not try to make everything work at once):

1. Main menu and all its dialogs.
2. Adventure map (tiles, objects, heroes, fog, routes, radar, side panel).
3. Battle screen.
4. Castle screen and all castle dialogs.
5. Hero dialog, army/artifact bars, market, puzzle, campaign screens, editor.

Verify per milestone: the screen renders with 2x assets without misalignments; a checklist of every
dialog reachable from that screen is kept in the status file and ticked.

### P3.5 — HD pack build tool

Files: new `src/tools/hdpack.cpp` (built with `ENABLE_TOOLS`), new `script/hdpack/` with a Python
driver, `docs/HD_PACK.md`.

Pipeline (run by the user on their own machine):

1. `extractor` → `icn2img` to dump every ICN frame as PNG with alpha and its offset (`icn2img` already
   writes offsets; verify the format and extend it to write a JSON sidecar `{ "x":..,"y":..,"w":..,"h":.. }`).
   Also dump the 5 cycling variants for sprites containing cycling indices (P3.2) and the indexed
   source itself as a palette PNG (needed for recolour masks, P3.3).
2. Upscale with an AI model, 2x, via the Python driver (`script/hdpack/upscale.py`): default backend is
   Real-ESRGAN (`realesrgan-ncnn-vulkan` executable, model `realesr-animevideov3` for sprites with the
   alpha channel split off and upscaled as a separate greyscale image, then recombined), with a pure
   xBRZ fallback (`hdpack --xbrz`) that needs no ML runtime. The driver processes the alpha matte
   separately and re-thresholds it at 0.5 to keep crisp sprite silhouettes.
3. Post-process rules in the driver: sprites smaller than 8x8 (cursors, icons) → xBRZ only; fonts →
   xBRZ only (AI ruins glyphs); map tiles → upscale the whole terrain tileset as one atlas to avoid tile
   seams, then re-cut; UI borders → upscale, then straighten 1 px lines with a simple vertical/horizontal
   run-length pass.
4. `hdpack` C++ tool packs the result into `fheroes2_hd.h2d` (RGBA32 sprite entries: extend
   `writeImageToH2D`/`readImageFromH2D` in `src/engine/h2d_file.cpp` with a format byte, keep backward
   compatibility for the existing indexed entries) with names `ICNNAME/index` and `ICNNAME/index#cycleN`.
   Store a manifest entry with the source AGG checksum so a pack from a different game version is
   rejected with a clear message.
5. `game_assets.cpp`: in HD mode, `Assets::getImage()` looks up `fheroes2_hd.h2d` first; if the sprite is
   missing it loads the original, upscales it 2x with xBRZ (Phase 1 scaler) at load time, converts to
   RGBA32, and caches it. This guarantees a complete game from the first partial pack.
6. `docs/HD_PACK.md`: step-by-step user instructions, hardware requirements, expected run time, and
   the legal note that the pack is derived from the user's own game files and must not be shared.

Verify: build a pack from the demo data with the xBRZ fallback (CI can do this: run the demo download
script, run the pipeline, start the game headless for 5 seconds with `--hd`). Build a pack with
Real-ESRGAN locally and compare visually.

### P3.6 — Settings, command line and memory

Files: `settings.{h,cpp}`, `src/fheroes2/game/fheroes2.cpp` (argument parsing), `dialog_graphics_settings.cpp`.

1. `--hd` command-line flag and `hd assets = on/off` config key; the dialog option is shown only when
   `fheroes2_hd.h2d` is found in the data directories.
2. HD mode requires the display to be RGBA32 and `assetScale() == 2`; switching the toggle restarts
   the renderer and flushes the asset cache (restart of the application is acceptable; show a message).
3. Memory: 2x RGBA assets are 16x the bytes of indexed 1x. Keep the lazy cache but add an LRU eviction
   in `_icnVsSprite` for HD mode with a budget of 1 GiB (configurable, `hd cache mb`). Disable HD mode on
   Vita and Switch at compile time.

### Phase 3 exit criteria

* Full campaign and a multiplayer hot-seat game playable start to finish in HD mode with the xBRZ
  fallback pack; no indexed-only code path on the adventure map hot loop.

* Indexed mode screenshot-identical to `master` on the same commit.
* Peak RSS in HD mode under 2 GiB on a 1920x1080 adventure map after one hour of play.
* The per-screen checklist from P3.4 fully ticked.

---

## Appendix A — Effort and dependencies

| Phase | Tasks | Effort | Depends on |
| --- | --- | --- | --- |
| 1 | P1.1–P1.6 | 2 to 4 weeks | none |
| 2 | P2.1–P2.6 | 1 to 3 months | P1.1 (scaling enum), P1.3 (scaler reused by P2.6) |
| 3 | P3.1–P3.6 | 6 to 12 months | P1.3 (fallback scaler), P2.6 optional |

## Appendix B — Files most frequently touched

| Area | File |
| --- | --- |
| Render backends, resolution logic | `src/engine/screen.cpp`, `src/engine/screen.h` |
| Image primitives | `src/engine/image.cpp`, `src/engine/image.h` |
| Palette tables | `src/engine/pal.cpp`, `src/engine/pal.h` |
| Colour cycling | `src/engine/render_processor.cpp` |
| Asset cache | `src/fheroes2/game/game_assets.cpp` |
| Settings | `src/fheroes2/system/settings.cpp`, `settings.h` |
| Graphics UI | `src/fheroes2/dialog/dialog_graphics_settings.cpp` |
| Adventure map drawing | `src/fheroes2/gui/interface_gamearea.cpp` |
| Project asset container | `files/data/resurrection.h2d`, `src/tools/h2dmgr.cpp` |

## Appendix C — Non-goals

* No change to game rules, AI, map format or save format.
* No redistribution of upscaled original art.
* No 3D, no mesh or lighting engine. "DLSS-like" here means render-at-lower-resolution plus
  reconstruction upscaling, implemented in 2D.
