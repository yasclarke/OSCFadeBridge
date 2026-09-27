# OSC Fade Bridge

A small macOS app that bridges QLab (or anything else that speaks OSC) to an OSC-controlled mixing desk, handling fades in between.

It listens for OSC messages on custom addresses carrying `[x, y]` — a destination value and a fade time in seconds — and ramps from the last value it received to the new one over that duration, sending updates at a configurable rate to one or more mapped, independently-scaled output addresses.

## Features

- Custom input addresses, each mapped to one or more output addresses with independent input/output scale ranges (e.g. a 0–1 fader driving both a 0–1 output and a −60–0 dB output at once)
- Configurable OSC receive port, send host/port, and update rate
- Undo/redo for every edit
- Multiple named projects (New / Open / Save / Save As), each with its own mappings and settings
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
