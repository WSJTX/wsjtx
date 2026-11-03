# WSJT-X for Android

A native Android/Kotlin port of [WSJT-X](https://wsjt.sourceforge.io/), ported from the iOS Swift implementation in `../ios/`.

## Architecture

```
android/WSJTX-Android/
└── app/src/main/java/com/wsjtx/android/
    ├── models/        — RadioMode, Band, Q65Config, OperatingConfig, QSORecord, DecodedMessage
    ├── audio/         — AudioEngine (AudioRecord + AudioTrack)
    ├── dsp/           — FFTProcessor, WaterfallData
    ├── decoder/       — FT8Decoder, FT8Protocol, FT8MessagePacker
    ├── encoder/       — FT8Encoder, Q65Encoder
    ├── network/       — RigControl (Hamlib TCP), PSKReporter
    ├── ui/
    │   ├── screens/   — MainScreen, WaterfallScreen, MessagesScreen, LogbookScreen, SettingsScreen
    │   ├── components/ — WaterfallCanvas, LevelMeter, FreqSlotBar, Q65ConfigPanel, CycleTimer
    │   └── theme/     — WSJT-X dark colour theme
    └── utils/         — GridLocator, TimeSync, ADIFExporter
```

## Tech Stack

| Component | Library |
|---|---|
| UI | Jetpack Compose |
| Audio capture | `AudioRecord` (44.1/48 kHz) |
| Audio playback | `AudioTrack` |
| FFT | Pure-Kotlin Cooley-Tukey with Hann window |
| Network | `java.net.DatagramSocket` (UDP), `java.net.Socket` (TCP) |
| Location | `FusedLocationProviderClient` |
| Settings | `DataStore<Preferences>` |
| Build | Gradle (Kotlin DSL) |

## Requirements

- Android Studio Hedgehog (2023.1) or newer
- Android API 26+ (Android 8.0+)
- A licensed amateur radio callsign for transmitting

## Build

```bash
cd android/WSJTX-Android
./gradlew assembleDebug
# Install:
adb install app/build/outputs/apk/debug/app-debug.apk
```

## Features

| Feature | Status |
|---|---|
| FT8 TX / decode | ✅ |
| FT4 TX | ✅ |
| Q65 TX (period-aware) | ✅ |
| FFT waterfall + period lines | ✅ |
| RX/TX freq markers + tap-to-tune | ✅ |
| 1st/2nd TX slot | ✅ |
| Bands 160m – 24 GHz | ✅ |
| QSO logging (ADIF) | ✅ |
| Hamlib rig control (UDP) | ✅ |
| PSK Reporter | ✅ |

## License

GNU General Public License v3.0 — same as the upstream WSJT-X project.
