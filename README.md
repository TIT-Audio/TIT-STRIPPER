# TIT STRIPPER

**Free channel strip (VST3 for Windows and macOS, AU for macOS)** with three modules in one plugin: **Gate/Expander**, **TIT COMP** (the compressor with all its modes) and **TIT Q** (the 10-band dynamic equaliser). Reorder the modules as you like and save the whole strip as a preset. No registration, no ads, no limits.

## How it works
- The top bar shows the three modules in chain order. Each has an **On/Off** button, a name button that shows its window and two arrows **< >** to move it in the chain. All six orders are allowed.
- **Presets** of the whole strip (three modules, On/Off and order): drop-down, *Save* button and the `...` menu to save or load anywhere (USB drive, cloud) or open the presets folder (`Documents/TIT STRIPPER/Presets`, `.titstrip`). "Initialize" restores factory values. Each module keeps its own presets too.
- **Sidechain**: one external sidechain input per instance, shared; the gate, the compressor and the dynamic EQ bands each choose internal or external.
- Every control of the three modules can be automated from the DAW (names start with `Gate`, `Comp`, `EQ`).
- The latency reported to the DAW is the sum of the active modules (oversampling, lookahead).

## Start-up settings (config file)
The `...` menu has **Start order for new instances**, **Use the current order as start order**, **Color on at start** and **Open the settings file**. They are saved in `config.txt`, which you can also edit by hand:

    order = EQ, GATE, COMP
    color = on

- `order`: order of the three modules for a new instance (`GATE`, `COMP`, `EQ`, left to right). If the line is missing or invalid, the default `GATE, COMP, EQ` is used.
- `color`: whether the compressor's Color button starts on or off.

Location: Windows `%APPDATA%\TIT Audio\TIT STRIPPER\`, macOS `~/Library/Application Support/TIT Audio/TIT STRIPPER/`. Existing projects keep their own saved order and state.
Factory start: Gate and Comp off, EQ on (no bands, transparent), order Gate > Comp > EQ, Color on.

## Modules
- **Gate/Expander**: threshold, range (up to 90 dB), ratio (1:1 to infinity = hard gate), attack, hold, release, hysteresis, lookahead (up to 10 ms), high-pass and low-pass detection filters with slopes from 6 to 96 dB/oct, 3-band graphic EQ with spectrum analyser, stereo link, internal/external sidechain and **SC Listen**. Curve display with the working point and a reduction meter.
- **TIT COMP**: modes Clean, VCA, FET, FET B, OP, FAIR, MU, DXX and DIGITAL, with the **Color** switch. See the TIT-COMP repository.
- **TIT Q**: 10 dynamic bands, character bands and minimum / linear / mixed phase. See the TIT-Q repository.

## Install
Download the latest build from *Actions* → artifact `TITSTRIPPER-all`, copy `TIT STRIPPER.vst3` to your VST3 folder and rescan in your DAW.
Unsigned macOS builds: `xattr -cr "TIT STRIPPER.vst3"` (or `.component`).

## Build from source
`cmake -B build && cmake --build build --config Release --target TITSTRIPPER_VST3`. Needs CMake 3.22+ and a C++17 compiler; JUCE 8 is downloaded automatically.
Layout: `strip/` chain and window, `gate/`, `comp/`, `q/` modules, `common/` shared code, `tests/` core tests (no JUCE) and `tests/headless/` a JUCE test of the full chain, compiled by hand.

## License
Free software under the **GNU AGPLv3** (see `LICENSE`), built with JUCE 8. The source code is in this repository.
All trademarks belong to their owners; TIT STRIPPER is not affiliated with or endorsed by anyone.

---

## Resumen en español
TIT STRIPPER es un channel strip gratuito (VST3 y AU) con Gate/Expander, TIT COMP y TIT Q en un solo plugin, con el orden de los módulos configurable y presets de todo el conjunto. El menú `...` permite elegir el orden y el estado de Color con los que arrancan las instancias nuevas (se guarda en `config.txt`). Licencia AGPLv3.
