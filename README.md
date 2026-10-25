# epsilon-common

This repo holds the code shared by the Epsilon Audio plugins (Monolith, Caldera, Obsidian, Strata, Basalt, Gneiss, Mica, Flint, Granite, Magma, Schist, Slate, Talc, Geode and Malachite). It is header-only C++17 under the MIT license.

Plugins don't depend on this repo when they build. Each plugin keeps a **vendored copy** in `plugins/<Name>/common/`, so its repo still builds on its own and its CI doesn't change. The `VERSION` file in that folder records which epsilon-common commit the copy came from.

## Contents

| Header | What it does | Depends on |
|---|---|---|
| `EqBand.hpp` | EQ band engine v2: bell, low and high shelf, low and high cut, notch, band pass, tilt shelf, flat tilt and all pass shapes. Slopes run from 6 to 96 dB/oct in 6 dB steps, plus a brickwall. Each band is described as analog sections, which give both the exact analog response and a zero-latency TPT state-variable cascade | — |
| `Convolver.hpp` | Uniformly partitioned FFT convolution that cross-fades between kernels | `Character.hpp` | Analog character stage. Modes: Warm (asymmetric, even harmonics), Punchy (symmetric soft clip), Vintage (warm, plus tone and slow random drift). Small signals pass at unity, so drive changes the colour, not the level | `EqBand.hpp` |
| `Loudness.hpp` | ITU-R BS.1770-4 / EBU R128 loudness meter: K-weighting at any rate; momentary, short-term and gated integrated loudness; loudness range; 4x true peak. Checked against EBU Tech 3341 and 3342 cases | `Halfband.hpp` |
| `Fft.hpp` |
| `CurveFit.hpp` | Fits bells and shelves to a target curve: greedy placement, then Levenberg-Marquardt. Used by EQ Sketch and EQ Match | `EqBand.hpp` |
| `Character.hpp` | Analog character stage. Modes: Warm (asymmetric, even harmonics), Punchy (symmetric soft clip), Vintage (warm, plus tone and slow random drift). Small signals pass at unity, so drive changes the colour, not the level | `EqBand.hpp` |
| `Loudness.hpp` | ITU-R BS.1770-4 / EBU R128 loudness meter: K-weighting at any rate; momentary, short-term and gated integrated loudness; loudness range; 4x true peak. Checked against EBU Tech 3341 and 3342 cases | `Halfband.hpp` |
| `Fft.hpp` | Radix-2 complex FFT for analyzers and FFT convolution | — |
| `Halfband.hpp` | Linear-phase halfband oversampling. `Oversampler` covers 2x and 4x; `OversamplerN` chains stages up to 32x. Latency is given in base-rate samples | — |
| `ModSystem.hpp` | Modulation system v2: four XLFOs (seven waves or a 16-step sequencer, free or tempo-synced), two ADSR envelope generators (MIDI- or audio-triggered), two envelope followers (input or sidechain), two XY pads and five MIDI sources, and a slot matrix where a slot can modulate another slot's amount. The plugin chooses where the parameters live (`Layout`) and what the destinations are | — |
| `Platform.hpp` | `openURL`, `nowSec`, and `typedChar`, which applies Shift to DPF key events | Windows: link `-lshell32` |
| `UserPresets.hpp` | `PresetStore`: user preset files (plain `symbol=value` text) in the standard folder for each OS. Saving, listing, loading and deleting them, with names cleaned up for the file system | — |
| `UndoHistory.hpp` | Undo and redo (200 steps), plus A/B with copy. Edits are recorded as gestures | — |
| `UiKit.hpp` | `Theme`, `drawHeaderButton`, `drawTooltip`, `TipTimer` for hover tips, `Flash` for status messages, and `HelpWindow`, the HELP window | DPF/NanoVG. Include it after `DistrhoUI.hpp` |

Preset folders:

| OS | Folder |
|---|---|
| macOS | `~/Library/Audio/Presets/Epsilon Audio/<Product>` |
| Windows | `%APPDATA%\Epsilon Audio\<Product>\Presets` |
| Linux | `~/.config/epsilon-audio/<product>/presets` |

## Using it in a plugin

```sh
# from this repo, with a clean checkout
tools/sync.sh ../slate            # copies common/*.hpp to ../slate/plugins/Slate/common/
```

```cpp
#include "common/UndoHistory.hpp"
#include "common/UiKit.hpp"
```

To change shared code, edit it **here**, run the tests, commit, then sync it into each plugin as that plugin is updated. Don't edit the vendored copies.

## Rules

- **Append-only behaviour.** Anything that touches saved sessions keeps loading old sessions the same way: preset file format, parameter handling, filter coefficients. A behaviour change needs a new name or a version flag.
- **No framework in the DSP and data headers.** Only `UiKit.hpp` uses DPF.
- **Public information only.** This is clean-room code: nothing is copied from commercial products.

## Tests

```sh
c++ -std=c++17 -O2 -Wall -Wextra -o test_common tests/test_common.cpp && ./test_common
c++ -std=c++17 -O2 -Wall -Wextra -o test_eq tests/test_eq.cpp && ./test_eq
c++ -std=c++17 -O2 -Wall -Wextra -o test_dsp tests/test_dsp.cpp && ./test_dsp
c++ -std=c++17 -O2 -Wall -Wextra -o test_mod tests/test_mod.cpp && ./test_mod
```

CI covers the following:
- Runs the unit tests on Linux and macOS: a preset round trip, undo/redo and A/B, halfband passband, stopband and latency, the FFT, the convolver, typed characters, and the EQ engine. The EQ tests check that every shape and slope meets its specification and matches Mica 1.x, that the digital filters track the analog response, and that curve fitting works. The DSP tests cover the oversampler from 2x to 32x, the loudness meter (EBU test cases, gating, LRA, true peak, K-weighting against the reference coefficients) and the character stage.
- Compiles `UiKit.hpp` against the current DPF.
- Builds the framework-free headers for Windows with MinGW.
- Exercises `sync.sh`.

## License

MIT. See [LICENSE](LICENSE).
