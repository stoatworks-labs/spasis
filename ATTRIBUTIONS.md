# Attributions

spasis is MIT licensed. It builds on the following.

## Submodules

| Project | Licence | Used for |
|---|---|---|
| [Resolume FFGL SDK](https://github.com/resolume/ffgl) | MIT | The plugin API, `ffglex` GL helpers, the screen quad and shader wrapper |
| [miniaudio](https://github.com/mackron/miniaudio) (v0.11.25, David Reid) | Public domain / MIT-0 | Capture device enumeration and the audio callback, on CoreAudio, WASAPI and ALSA |

## Code carried across from sibling repos

Written for the Stoatworks fleet, copied rather than shared because four
bundles each link their own copy of everything and a shared library between
repos would be a dependency for the sake of a hundred lines.

| File | From |
|---|---|
| `source/render/ScopeBuffer.{h,cpp}` | [resolume-scopes](https://github.com/stoatworks-labs/resolume-scopes), via [vectrix](https://github.com/stoatworks-labs/vectrix) |
| `source/render/GLState.h` | resolume-scopes, via vectrix |
| `source/Diag.{h,cpp}` | vectrix |
| `source/StoatworksAbout*.h` | stoatworks-backend/about |
| the PNG writer and CGL context in `tools/sptest/Render.cpp` | vectrix's `vxtest` |

## Prior art the displays answer to

spasis contains no code from any of these. They are named because the
instruments are conventions, and a meter that disagrees with the convention is
simply wrong — the landmarks in `sptest --field` and `sptest --width` are there
to keep spasis agreeing with them.

- iZotope **Ozone Imager** — polar sample and polar level
- Mastering The Mix **LEVELS** — stereo field, **REFERENCE** — width against frequency
- Mastering The Mix **STEREOVAULT** — per-band mid/side
