# spasis

Sonar-style audio field visualisations as four FFGL **sources** for Resolume
Arena/Avenue. C++/GLSL, CMake MODULE → universal `.bundle` (macOS) + Windows
`.dll`. Public MIT repo.

Read `AGENTS.md` before changing the field law or the analysis.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build`
- Install to Resolume: `cmake --install build`

## Verify
- Everything: `tools/verify.sh`
- The invariants: `./build/sptest`
- One group: `./build/sptest --field` (also `--width --fft --lfe --ring --fallback --shaders`)
- Pictures: `./build/sptest --sheet docs/sheet 2` (and `... 6` for surround)
- Video frames: `./build/sptest --movie <shot> [secs] [chans] [w] [h] [hue]` —
  raw RGBA on stdout for ffmpeg. Shot names are the contact sheet's, from one
  shared table, so a shot is always a configuration the sheet also checks.
  **Nothing else may print to stdout in this mode.**
- Instantiate sweep: `../resolume-ofx-bridge/build/ffgltest "build/Spasis Field.bundle"`

## Notes
- **FFGL cannot supply this.** Its only audio input is `FF_USAGE_FFT` — N
  magnitude bins of a **mono sum**, no phase, no channels, no time domain. That
  is why spasis opens its own capture device, and why the host path is a
  fallback that draws tonal balance and nothing else.
- **The stereo layout is ±45°, not ±30°.** At 45° the generalised field law is
  exactly the classic goniometer. At the ITU angles it is not. `sptest --field`
  fails if this moves.
- **Width is `1 - |ΣX|² / (N·Σ|X|²)`.** It agrees with `|S|/(|M|+|S|)` at every
  landmark and keeps working past two channels. Do not swap in an M/S formula.
- **LFE contributes no direction**, only a level. Including it swings the whole
  field to whatever angle the layout table lists.
- **Never pass `nullptr` to `CFFGLPlugin::InitGL`.** It does `currentViewport =
  *vp` with no null check; the optimiser then deletes everything after the call
  including the return, and the plugin loads, logs its way through InitGL, and
  fails to instantiate with no message anywhere.
- **`gl_PointCoord` is undefined outside `GL_POINTS`.** On Apple's Metal GL it
  reads (0,0), which zeroes a radial falloff — every line and bar renders black.
  The trace shader branches on a `pointMode` uniform.
- **Set `params_[]` before declaring the parameter.** `SetParamInfof` declares
  a parameter using `GetFloatParameter( index )` as its default, so a block of
  assignments after the declarations sets the plugin's idea of the value and
  never reaches the host. Five controls shipped at zero that way.
- **An option element must never be left unnamed.** Resolume draws a blank row.
  Spare device slots are labelled `(none)`.
- **Azimuths are signed, and a negative one is on the left.** In the full circle
  the plot parameter has to WRAP, not be range-checked — checking dropped every
  graticule mark on the left half of a 5.1 layout.
- `Ring::overwritten()` is **not** an error count and grows continuously by
  design; the consumer never advances the read cursor.
- Devices are remembered **by name, never by index**. An index is a position in
  a list that changes whenever anything is plugged in.
- A parameter change must not clear the capture ring; a *geometry* change must
  clear the persistence.
- `sample`, `input`, `output`, `filter`, `common`, `active` are GLSL reserved
  words. Shader errors surface only at runtime, in the diagnostics log.
- Piping a build through `| head` can SIGPIPE the build half-way and leave a
  stale bundle that looks freshly built. Do not do it when the result matters.
- macOS build must be universal. Verify with `lipo`, never the build log.
- Public repo. "Commit" = commit **and** push.

## Diagnostics
`source/Diag.{h,cpp}` — log file only, no crash handler (this runs inside
Resolume). `~/Library/Logs/spasis/`. It exists for the failures that are
otherwise invisible: a shader that will not compile, and a capture device that
opens, runs, and returns digital zero for ever because the host has no
microphone permission.
