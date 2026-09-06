# demo/ — the browser demo

Live at **https://spasis-demo.stoatworks-labs.com**, linked from the
[project page](https://stoatworks-labs.com/software/spasis/) and from the
[Resolume suite page](https://stoatworks-labs.com/resolume/).

**This is not the plugin.** It is the GLSL from
[`source/render/Shaders.h`](../source/render/Shaders.h), copied across unedited,
plus JavaScript ports of the analysis in [`source/analysis/`](../source/analysis)
and the geometry in [`source/render/Builder.cpp`](../source/render/Builder.cpp),
run in WebGL2 with the parameters the plugins' constructors declare. The page
says so in a banner, and lists what it does not reproduce at the foot.

## Why this one carries more than its siblings

The other demos in the kit are a fragment shader over a texture: the shader *is*
the plugin, so running it for real is most of the job. Spasis is not shaped like
that. Its shaders are three small passes — a trace, a decay and a composite — and
everything that makes it interesting happens before them, on the CPU: the FFT,
the field law, the width figure and the mesh construction. Showing only the
shaders would be showing the least of it.

So the analysis and the builder are ported rather than skipped, and ported
literally — same band edges, same ballistics, same `place()`, same hash. **Where
`plugin.js` and the C++ disagree, the C++ is right and this is a bug.**

That is a real maintenance cost and it is worth stating plainly: a change to
`Analyser.cpp` or `Builder.cpp` has to be made here too, and nothing enforces it.
The same is already true of the shaders in every demo in this kit.

## Two things it cannot do, and does not pretend to

- **It has no capture device.** The plugins open their own multichannel input —
  that is the whole reason they exist in the shape they do, because FFGL hands a
  plugin nothing but a mono magnitude spectrum. A web page cannot open a Dante
  receiver, so this one generates the same synthetic programme the offline
  harness and the project video use. **Audio Input** and **Rescan Inputs** are
  therefore shown but inert; they are two of the plugin's real controls and a
  demo that quietly dropped them would be describing a different plugin.
- **The host-FFT fallback is not reproduced.** In the plugin, leaving Audio Input
  on `Resolume (mono)` leaves Field, Rose and Width deliberately empty, because a
  mono magnitude spectrum cannot feed them.

## Editing it

- `plugin.js` — the parameters, the ported analysis and geometry, and the
  shaders. **When anything in `source/analysis/`, `source/render/Builder.cpp` or
  `source/render/Shaders.h` changes, change it here too.** The two copies exist
  because the demo cannot include a C++ file; nothing enforces that they agree.
- `vendor/` — the shared kit, vendored from `stoatworks-backend/resolume-demo/`.
  **Do not edit these.** Fix the master and re-run `./sync.sh`; `./sync.sh
  --check` reports drift.

## Deploying

No build step. From the repo root:

```bash
cf-run npx wrangler deploy
```

Then verify by content rather than by status code — a wrong page still answers
200:

```bash
curl -s 'https://spasis-demo.stoatworks-labs.com/?cb=1' | grep -o '<title>[^<]*'
```
