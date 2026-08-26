# spasis — working notes

## Status (2026-08-26)

Built from nothing in one session. Four bundles build universal, install into
Resolume, pass the fleet's `ffgltest` instantiate sweep, and render twelve
display/shape/style combinations offline. Thirty invariant checks pass.

**Not yet done, in the order it matters:**

1. **Never run in Resolume.** Installed into `Extra Effects`, never launched.
   The first live session is where the FFGL parameter panel, the device
   dropdown and the microphone-permission prompt get tested, and none of those
   can be reached from a harness.
2. **Never captured from a real device.** All audio so far is synthetic, fed
   directly into the ring. CoreAudio has been linked but not opened in anger;
   WASAPI and ALSA have not been compiled.
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
   "Built target". Two rounds of debugging a binary that had not been relinked.
3. `gl_PointCoord` outside `GL_POINTS` — every line and bar rendered black while
   particles looked perfect.
4. `ffgl::sdk` is the CMake target name, not `ffgl`.
5. `CFFGLPlugin::SetTime` returns `FFResult`, not `void`.

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
