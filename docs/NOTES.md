# spasis — working notes

## Status (2026-08-26)

Built from nothing in one session, then tested in Resolume Arena 7.27.1 on both
platforms. 300 invariant checks, a twelve-picture contact sheet, universal macOS
bundles, MSVC x64 DLLs, and the fleet's `ffgltest` instantiate sweep all pass.

**Run in the real host, both platforms:**

- **macOS**, Arena 7.27.1 on an M4 Max. All four register as Video Sources
  (`SP01`-`SP04`, category 3), expose 22 parameters with correct names, types
  and defaults, and **capture live audio from a real CoreAudio device** — a
  mono microphone drew a correctly vertical goniometer trace. Multichannel
  capture verified against real hardware with `sptest --devices`: a 6-channel
  bridge opens as 6 at 48 kHz, and a 112-channel L-ISA bridge is correctly
  capped to 8 at 44.1 kHz.
- **Windows**, Arena 7.27.1 on win-lab under **Mesa llvmpipe**. All four DLLs
  load (`Done scanning directory, loaded 4 plugin(s)`), register as category 3,
  and instantiate — which means every shader compiles and links under a GLSL
  compiler that is not Apple's. The box has no audio device at all, which
  exercised the empty-device path: the dropdown shows only `Resolume (mono)`
  plus `(none)` spares, and nothing crashes.

**Not yet done, in the order it matters:**

1. **Never fed by a real desk.** A microphone and a silent loopback are not a
   5.1 stem feed. The layouts, the rose and the width plot have only ever seen
   synthetic multichannel audio.
2. **Never run a show.** No sustained run, no performance figures on a real GPU
   (llvmpipe says nothing about speed), no VJ has touched the controls.
3. No CI, no release workflow, no website entry, no `.github/`.
4. `StoatworksAbout.h` is **hand-written**, not generated — spasis is not in
   `projects.json`. Register it and re-run `sync-about.py`.
5. No `tools/verify.sh` yet; README references it.
6. No factory presets. The fleet's preset pattern (copy-based apply does not
   work in Resolume) applies when they are added.

## Decisions already made

**Own capture with a host-FFT fallback**, rather than host-only or capture-only.
Host-only cannot draw three of the four plugins at all; capture-only draws
nothing until the operator has routed audio, which is a bad enough first run to
lose the plugin. See AGENTS.md.

**One source per measurement**, not one plugin with a measurement dropdown.
Matches how VJs browse the source list, and keeps each parameter set inside
FFGL's 16-character name limit. All four declare the *same* parameter ids in the
same order — including `Lobes`, which only Rose uses — so that a parameter never
sits at a different index in different bundles.

**Stereo at ±45°.** The single most consequential number. See AGENTS.md.

**No OFX target.** See AGENTS.md, "things deliberately not done".

## Traps that have already bitten

Each is written up in AGENTS.md with the reasoning. In the order they cost time:

1. `nullptr` to `CFFGLPlugin::InitGL` — UB, optimiser deletes the rest of the
   function, plugin fails to instantiate with no message.
2. A build pipeline truncated by `| head`, leaving a stale bundle that reported
   "Built target". Two rounds of debugging a binary that had not been relinked,
   and it recurred later when a restored file and a build stamp landed in the
   same second.
3. `gl_PointCoord` outside `GL_POINTS` — every line and bar rendered black while
   particles looked perfect.
4. `ffgl::sdk` is the CMake target name, not `ffgl`.
5. `CFFGLPlugin::SetTime` returns `FFResult`, not `void`.

### Found by first contact with the host, after everything above passed

The fleet note about hardware first contact held exactly: five more bugs, none
of which any offline check could see, all found in the first hour in Arena.

6. **Five controls defaulted to zero.** `SetParamInfof` reads `params_` for the
   default, and `params_` was assigned after the declarations.
7. **Thirteen blank rows** at the end of the device dropdown, from unnamed spare
   option elements.
8. **Every graticule mark on the left was missing**, because a negative azimuth
   gives a negative plot parameter that has to wrap rather than be range-checked.
9. **`Ring::dropped()` was meaningless** and alarming — it counts history
   scrolling past, not lost audio, because the consumer never advances the read
   cursor. Renamed `overwritten()` and documented.
10. **The README described behaviour that did not exist** — it claimed the
    directional displays "say so on screen" on the fallback path. They rendered
    black. That is what the graticule now exists for.

6, 7 and 8 are now gated by `sptest --params` and `sptest --graticule`, both of
which were checked against the broken code before being committed.

### Still open

**`Capture::open()` blocks the render thread.** It runs inside `ProcessOpenGL`,
and while CoreAudio opens a device Arena drops parameter writes — reproduced
over REST: four writes returned 204 and none of them took, then all four
succeeded first time a moment later. Acceptable for a one-off device choice,
wrong if anyone changes device mid-show.

## Fleet notes that applied

- [ffgl audio bpm patterns](https://github.com/stoatworks-labs/fleet-notes/blob/main/notes/reference_ffgl_audio_bpm_patterns.md)
  — the `FF_USAGE_FFT` mechanism, and that Resolume sends `SetTime` in
  **milliseconds**. spasis auto-detects the unit from the first plausible delta.
- [ffgl 16 char param names](https://github.com/stoatworks-labs/fleet-notes/blob/main/notes/reference_ffgl_16_char_param_names.md)
  — every parameter name here is inside the limit; check again when adding one.
- [ffgl instantiate sweep trap](https://github.com/stoatworks-labs/fleet-notes/blob/main/notes/reference_ffgl_instantiate_sweep_trap.md)
  — the About `SetTextParameter` handler.
- [new plugin repo copy traps](https://github.com/stoatworks-labs/fleet-notes/blob/main/notes/reference_new_plugin_repo_copy_traps.md)
  — `vcpkg.json` is present; `InfoOFX.plist.in` is not, because there is no OFX
  target to get it wrong.
- [audio input over ssh tcc](https://github.com/stoatworks-labs/fleet-notes/blob/main/notes/reference_audio_input_over_ssh_tcc.md)
  — will matter the first time capture is tested from a remote shell.

## Worth adding to fleet-notes

The `CFFGLPlugin::InitGL(nullptr)` trap is not repo-specific — it is an SDK
trap, it is silent, and it looks exactly like a shader failure. It belongs in
`reference_ffgl_sdk_bugs.md`.
