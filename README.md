# epsilon-common

This repo holds the code shared by the Epsilon Audio plugins (Monolith, Caldera, Obsidian, Strata, Basalt, Gneiss, Mica, Flint, Granite, Magma, Schist, Slate, Talc, Geode and Malachite). It is header-only C++17 under the MIT license.

Plugins don't depend on this repo when they build. Each plugin keeps a **vendored copy** in `plugins/<Name>/common/`, so its repo still builds on its own and its CI doesn't change. The `VERSION` file in that folder records which epsilon-common commit the copy came from.

## Contents

| Header | What it does | Depends on |
|---|---|---|
| `Fft.hpp` | Radix-2 complex FFT for analyzers and FFT convolution | — |
| `Halfband.hpp` | 2x/4x oversampling with linear-phase halfband filters. `Oversampler::latency(os)` gives the latency in base-rate samples | — |
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
```

CI covers the following:
- Runs the unit tests on Linux and macOS: a preset round trip, undo/redo and A/B, halfband passband, stopband and latency, the FFT, and typed characters.
- Compiles `UiKit.hpp` against the current DPF.
- Builds the framework-free headers for Windows with MinGW.
- Exercises `sync.sh`.

## License

MIT. See [LICENSE](LICENSE).
