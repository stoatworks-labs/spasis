# spasis — orientation for another LLM (or a newcomer)

Four FFGL sources that draw the stereo and surround field. This file is the
part that is not obvious from the code: the one idea, what falls out of it, and
the traps that have actually bitten.

`docs/NOTES.md` carries working notes — status, decisions already made.
Cross-cutting fleet knowledge lives in
[fleet-notes](https://github.com/stoatworks-labs/fleet-notes).

---

## The one idea

**The field point for one instant is the amplitude-weighted sum of the unit
vectors to each speaker.**

    p = Σ s_c · u_c

That is the entire directional model, it is written in exactly one place
(`Analyser::analyse`, in the sample loop), and everything else in the suite is
a view of it or a frequency-domain version of it.

### What falls out of it

**The classic goniometer, exactly — but only at ±45°.** With two speakers at
±45°, `u_L = (-r, r)` and `u_R = (+r, r)` for `r = 1/√2`, so

    p = ( r(R - L), r(L + R) ) = ( side, mid ) · r

Mono rides the vertical axis; out-of-phase lies flat on the horizontal; a
hard-panned channel sits on its own diagonal. Those are the conventions every
goniometer on earth obeys, and this law reproduces them without a special case.

Set the stereo layout to the ITU ±30° and the generalisation still *works* — it
just stops agreeing with the instrument. `sptest --field` asserts both: that
±45° gives 45°, and that ±30° gives 30°, so nobody can "fix" one without seeing
the other move.

**Surround for free.** Six speakers at the BS.775 angles produce a field that
points where the energy is. The surround level rose is the same idea drawn from
per-channel meters instead of per-sample positions — a genuinely different
measurement, which is why `Frame` carries both and the Rose plugin has a control
to pick.

**One width formula for any channel count.**

    width = 1 - |Σ X_c|² / ( N · Σ |X_c|² )

One minus the coherent fraction of the band's power. N correlated channels give
0, an anti-phase pair gives 1, and a hard-panned stereo signal gives 0.5 — which
is exactly what the `|S|/(|M|+|S|)` meter in every mastering suite reads for the
same signal. It is not a new quantity wearing a familiar name; it agrees with
the familiar one at every landmark and keeps working past two channels.

---

## The shape of it

    source/analysis/   Frame.h is the contract. Read it first.
    source/audio/      Capture (miniaudio) and a lock-free ring.
    source/render/     Builder is CPU-side geometry; Canvas is the GL.
    source/sources/    Four registrations, one per bundle.
    tools/sptest/      Invariants, shader check, and the contact sheet.

`Frame.h` is where the conventions both halves must agree about are written
down once — angle convention, what is normalised and what is not, why LFE is
excluded, and why the fallback path leaves fields empty instead of guessing.

**The geometry mapping is on the CPU, not in a vertex shader.** That is a
deliberate inversion of the usual advice. There are at most a few thousand
vertices in a frame, so it costs nothing — and it means `sptest` can assert
about the mapping without a GL context. A geometry that is wrong in a shader can
only be caught by looking at it.

---

## Traps

### FFGL gives you a mono spectrum and nothing else

`FF_USAGE_FFT` is the whole audio API: N magnitude bins of a mono sum. No phase,
no channels, no time domain. Three of the four plugins cannot be built on it at
all. This is the reason spasis carries a platform-audio dependency, which
vectrix's AGENTS.md explicitly deferred as "deserves to be designed rather than
bolted on" — this is that design.

The host path is kept as a fallback because a plugin that draws nothing until
the operator has found the right input in a dropdown gets deleted before it is
understood. It sets `Frame::live = false`, and it leaves `field`, `rose` and
`width` **empty** rather than approximating them. `sptest --fallback` asserts
that. A plausible-looking guess is the one outcome worse than an empty display.

### Never pass `nullptr` to `CFFGLPlugin::InitGL`

It does `currentViewport = *vp` with no null check. Passing null is undefined
behaviour, and the interesting part is *how* it fails: the optimiser is entitled
to assume UB does not happen, so it deletes every statement after the call —
including the return. The plugin then compiles clean, links, loads, exports
`plugMain`, logs its way successfully through the whole of `InitGL`, and fails
to instantiate with no message anywhere. Cost: most of an afternoon.

### `gl_PointCoord` is undefined outside `GL_POINTS`

It is tempting to use it unconditionally in a shader shared by points, lines and
triangles, and let whatever fixed value it takes fall somewhere harmless. On
Apple's Metal GL that value is (0,0) — the far corner of the disc — so a radial
falloff evaluates to zero and **every line and every bar in the suite renders
pure black** while the particle style looks perfect. The trace shader branches
on a `pointMode` uniform.

### Digital silence is the failure mode, not an error

On macOS a host without microphone permission does not get an error when it
opens an input. It gets a working device that returns zero for ever. So does an
unpatched Dante receiver, and so does a BlackHole device nothing is routed to.
All three are identical to the code and all three look like "the plugin is
broken" to the operator, so `Capture` reports `Silent` after three seconds of
bit-exact zero and the display says so.

Related, from fleet-notes: **macOS denies audio input to anything launched over
ssh and the denial is silent.** A capture harness run over ssh measures nothing
and reports success.

### The ring drops the oldest, never the newest

Overrun is expected, not exceptional. The display only ever wants the most
recent few thousand samples; if the render thread stalls for a second there is
nothing useful in the second-old audio. A ring that dropped the *newest* would
show a display that lags further behind the music the longer it runs and never
catches up. `sptest --ring` asserts the drop is exact.

### A parameter change must not clear the capture ring

The opposite of the fleet's GPU habit. Audio history survives knob moves. What
*must* be cleared is the render persistence, and only when the geometry or style
changes — those move every vertex somewhere else, so the history is a picture of
an instrument that is no longer on screen.

### Piping a build through `| head` can truncate it

`cmake --build … | grep -E "error:" | head` SIGPIPEs the build part-way and
leaves a stale bundle that reports "Built target". Two hours of this run were
spent debugging a binary that had not been relinked. The fleet already has this
one written down for `set -o pipefail` plus `grep -q`; it applies to any pipe
whose reader exits early.

### The SDK's traps, all still live

Every one of these is in fleet-notes and every one applies here: parameter names
are truncated to **16 characters** with no warning; a STANDARD default is
clamped into 0..1 before `SetParamRange` can widen it (`FF_TYPE_INTEGER` is
exempt, which is why Max Channels is one); `SetTextParameter` must return
`FF_SUCCESS` for the About block or no host can instantiate the plugin;
`instantiateGL` pushes every declared default back through the setters and
destroys the instance on the first `FF_FAIL`; `FFGLFBO::Release` leaks its
colour texture; and the plugin registration must live outside a STATIC library
or the linker drops it.

### The host's view of the parameters is not the plugin's view

Two bugs shipped past a clean build, three hundred invariant checks, a
twelve-picture contact sheet and the fleet's instantiate sweep, and both were
found by reading Resolume's own parameter list over REST:

**`SetParamInfof` declares a parameter using `GetFloatParameter( index )` as its
default.** It reads `params_`. A tidy block of assignments *after* the
declarations therefore sets the plugin's idea of the value and never reaches the
host — five controls opened at zero in every composition. The harness could not
see it because the harness reads `params_` too.

**An option element with an empty name draws a blank row.** The device dropdown
is padded with spare slots so that Rescan has somewhere to put a newly plugged-in
interface, and the spares arrived unnamed: a menu ending in thirteen blank lines.

Both are now checked by `sptest --params`, which reads the declared `ParamInfo`
table rather than `params_` — the same table the host reads. It also enforces the
16-character name limit, so that trap is caught before a release rather than by
a screenshot.

### Azimuth is signed, so half the speakers are at a negative angle

`ChannelPlacement::azimuth` is 0 ahead and **negative to the left**. Converting
one to the geometry's 0..1 plot parameter therefore yields a negative number for
every speaker on the left, and in the full circle that is a legal position that
has to **wrap**. Range-checking it instead silently dropped every graticule mark
on the left of a 5.1 layout — three of five — which reads as a lopsided design
choice rather than as a bug. `sptest --graticule` counts them.

### A bounds check after the About branch is dead code

`if( index >= PT_ABOUT_FIRST )` catches every id at or above the About block,
and every About id is below `PT_COUNT` — so a following `if( index >= PT_COUNT )`
can never fire, and the compiler deletes it. The bounds check goes **first**.

---

## Checking your work

`tools/sptest` drives the shipping classes. The invariant half needs no GL
context; `--shaders` and `--sheet` create a headless CGL 4.1 core context.

The landmark tests are the point of the harness — they are what turns the
paragraph at the top of this file into something a machine can check. See the
table in README.md.

`--sheet` renders twelve display/shape/style combinations and fails any that
come out blank or solid. It cannot tell you a picture is *right*, only that it
is a picture; look at the files.

---

## Things deliberately not done

**No OpenFX build.** Every sibling repo ships one, and it would be wrong here:
spasis captures live audio, and Resolve, Nuke and Vegas have no live audio to
capture. An OFX build would be four plugins that can only ever draw the
fallback path, which is one plugin's worth of function under four names.

**No layout editor.** Layouts are a dropdown plus an even ring. A user-defined
map of arbitrary Dante channels to arbitrary angles is the right answer for a
64-channel feed and it needs a UI that FFGL's parameter vocabulary cannot
express — it deserves designing, not bolting on.

**More than eight channels.** The audio callback's slice buffer is stack-sized
for eight. Sixteen is a constant change plus a wider `perChannel` array in the
band loop; the reason to wait is that nothing above eight has been *heard* yet.

**No correlation above two channels.** There is no agreed N-channel correlation,
and inventing one to fill the field would put a number nobody can interpret next
to three that they can. `bandCorr` is zero above stereo and documented as such.

**No smoothing of the field trace.** It is drawn at whatever rate the analysis
block gives, and the persistence does the rest — which is what a phosphor does.

**No text on screen.** FFGL gives a plugin no window and no way to draw text, so
the three states that produce an empty instrument — no device chosen, a device
delivering digital silence, and a directional display on the host-FFT fallback —
cannot be explained in words on the output. They are distinguished from a
crashed plugin by the graticule brightening instead, which is the most an API
with no text can honestly do. The diagnostics log carries the actual reason.

**Opening the device off the render thread.** `Capture::open()` runs inside
`ProcessOpenGL`, so choosing an input blocks Arena's render thread for as long
as CoreAudio takes — long enough, measured, that parameter writes issued in that
window are dropped by the host. It is a one-off cost when the operator picks a
device and it has not yet been worth a worker thread and the state machine that
comes with one, but it is the next thing to fix if anyone changes device during
a show.

---

## Conventions

Tabs. British spelling in prose. Comments explain *why*, and especially what
goes wrong — a comment that restates the code earns nothing. Public MIT repo:
"commit" means commit **and** push.
