# Calliope

Calliope is a local, push-to-talk voice transcriber for Linux. Hold a keyboard
shortcut to record from the default ALSA microphone, release it to transcribe
with [whisper.cpp](https://github.com/ggml-org/whisper.cpp), and Calliope pastes
the result into the currently focused application. No audio is sent to a
remote service.

It is designed for a Wayland desktop: clipboard integration uses `wl-copy` and
`wl-paste`, and typing is performed through a Linux `uinput` virtual keyboard.

## Requirements

- Linux with ALSA and a working default capture device
- A Wayland session with `wl-clipboard` (`wl-copy` and `wl-paste`)
- CMake 3.16 or newer, a C++20 compiler, Git, and the ALSA development files
- Permission to read `/dev/input/event*` and write `/dev/uinput`
- Internet access on the first CMake configure, so CMake can fetch
  `whisper.cpp`

On Arch Linux, the system packages are typically:

```sh
sudo pacman -S --needed base-devel cmake git alsa-lib wl-clipboard
```

Calliope uses the English Whisper medium model at
`vendors/whisper-medium.en/ggml-medium.en.bin` (about 1.5 GB). The model is
intentionally not stored in this repository; the executable is compiled to
use that exact local path.

## Build

From the repository root:

```sh
cmake -S . -B build
mkdir -p vendors/whisper-medium.en
bash build/_deps/whispercpp-src/models/download-ggml-model.sh medium.en vendors/whisper-medium.en
cmake --build build --parallel
```

The download helper uses an available `wget` or `curl` implementation. The
model stays local and is ignored by Git.

For an NVIDIA CUDA build, configure a separate build directory:

```sh
cmake -S . -B build-gpu -DCALLIOPE_ENABLE_GPU=ON
cmake --build build-gpu --parallel
```

CUDA must already be installed and usable by CMake. A GPU-enabled binary can
still run with GPU use disabled in the configuration.

## Run

Run Calliope from the repository root so it finds `keybinds.conf`:

```sh
./build/calliope
```

The program prints the selected keyboard event device and then waits for the
trigger. Hold the trigger while speaking; releasing it stops recording,
transcribes the captured audio, and pastes the text into the focused window.
The transcription is also printed to the terminal.

Calliope first places the result on the Wayland clipboard and sends
`Ctrl`+`Shift`+`V` through its virtual keyboard, then attempts to restore the
previous clipboard contents. If the clipboard tools are unavailable, it falls
back to typing characters directly. Keep the desired destination focused when
you release the trigger.

## Configuration

`keybinds.conf` is read again on every keyboard event, so edits take effect
without restarting the program. The defaults are:

```ini
gpu = true
trigger = RIGHTCTRL
exit = MOD + DELETE
```

- `trigger` is the hold-to-record combination.
- `exit` quits Calliope.
- `gpu` enables Whisper GPU inference only when Calliope was built with
  `-DCALLIOPE_ENABLE_GPU=ON`; set it to `false` for a CPU build.

Keys in a combination are joined with `+` and are case-insensitive. Supported
modifiers are `MOD` (either Super/Meta key), `SHIFT`, `CTRL`, `RIGHTCTRL`, and
`ALT`. Supported named keys are `DELETE`/`DEL`, `ENTER`/`RETURN`, `ESC`,
`SPACE`, `TAB`, `BACKSPACE`, `MICMUTE`, and `F1` through `F24`, plus letters
`A`–`Z` and digits `0`–`9`.

For example:

```ini
gpu = false
trigger = MOD + SPACE
exit = MOD + SHIFT + ESC
```

## Permissions and troubleshooting

Desktop sessions commonly restrict access to raw input and `uinput`. If
Calliope reports that it cannot open a keyboard device or `/dev/uinput`, grant
the account appropriate access according to your distribution's device-permission
policy, then log out and back in. Running it through `sudo` is usually a poor
fit for a Wayland desktop because the root process may not be able to access
your user clipboard session.

Other useful checks:

- Confirm `wl-copy` and `wl-paste` are installed and that `$WAYLAND_DISPLAY` is
  set when clipboard paste does not work.
- Check the default capture device with `arecord -l` if opening or recording
  from the microphone fails.
- Set `gpu = false` if a CPU-built binary cannot initialize GPU inference.
- Very short or silent recordings are deliberately ignored to avoid pasting
  empty transcriptions.

## Limitations

Calliope currently transcribes English only, uses ALSA's `default` capture
device, supports Wayland clipboard tooling, and identifies the first input
device that looks like a keyboard. It is therefore best suited to a personal
Linux desktop rather than a multi-seat or highly customized input setup.
