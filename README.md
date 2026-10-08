# OSC Fade Bridge

A small macOS app that bridges QLab (or anything else that speaks OSC) to an OSC-controlled mixing desk, handling fades in between.

It listens for OSC messages on custom addresses carrying `[x, y]` — a destination value and a fade time in seconds — or for MIDI messages, and ramps from the last value it received to the new one over that duration, sending updates at a configurable rate to one or more mapped, independently-scaled OSC or MIDI outputs.

## Features

- Custom input addresses, each mapped to one or more output addresses with independent input/output scale ranges (e.g. a 0–1 fader driving both a 0–1 output and a −60–0 dB output at once)
- Multiple named send targets — OSC hosts/ports and MIDI output ports — with each output choosing its own target
- MIDI inputs and outputs: 7-bit CC, 14-bit CC and pitch bend. MIDI inputs have a per-mapping fade time (0 = follow instantly, e.g. a hardware fader)
- VCA-style scalers: inputs with no outputs of their own (OSC or MIDI). A mapping scaled by one has its normalised value multiplied by the scaler's, mapped onto a per-mapping range (e.g. 0.9–1 for a gentle trim). Scalers can be named
- MIDI Learn: listen for the next CC or pitch bend (14-bit CC pairs detected automatically) to set an input's device, channel and message
- Live values for every input and output, and drag-to-change (or double-click to type) values in the lists for testing
- Configurable OSC receive port and update rate
- Undo/redo for every edit
- Multiple named projects (New / Open / Save / Save As), each with its own mappings and settings. Projects from earlier versions load with their send host/port as a single target
- Duplicate-address protection on inputs, with a warning for duplicate outputs

## Installing

Download the latest `.dmg` from [Releases](../../releases), open it, and drag **OSC Fade Bridge** into Applications.

The app isn't signed with a paid Apple Developer ID, so on first launch Gatekeeper will refuse to open it. Right-click (or Control-click) the app in Applications and choose **Open**, then confirm in the dialog that appears — you only need to do this once.

## Building from source

Requires CMake 3.22+ and Xcode command line tools. JUCE is fetched automatically.

```sh
cmake -B build
cmake --build build --config Release
```

The built app is at `build/OSCFadeBridge_artefacts/Release/OSC Fade Bridge.app`.

## License

MIT — see [LICENSE](LICENSE).
