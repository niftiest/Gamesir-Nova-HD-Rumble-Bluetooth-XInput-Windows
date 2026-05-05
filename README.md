# Gamesir Nova HD Rumble XInput

Make a **Gamesir Nova HD Rumble** Bluetooth controller appear as an **Xbox 360 / XInput**
pad on Windows (with rumble support) so that games which only support XInput
recognize it natively - no Steam Input shim required.

> Profile-driven: any HID gamepad can be supported by adding a JSON profile.
> Nova HD in Pro Controller Bluetooth mode is the profile shipped in v0.2 (with rumble);
> pull requests for other controllers welcome.

---

## What it does

Modern Windows games query the XInput API (Xbox 360 / Xbox One / XInput pads).
Many third-party Bluetooth controllers - including the Gamesir Nova HD Rumble - present as
generic HID devices over Bluetooth, which games using XInput-only do not see.

This program reads raw HID input from the controller and feeds it into a virtual
Xbox 360 pad created by the open-source [ViGEmBus](https://github.com/ViGEm/ViGEmBus)
driver. To games, the result looks identical to a real Xbox controller.

Key features:
- Bluetooth-only profile in v0.1.0 (`profiles/gamesir-nova-hd-bt.json`); USB and other
  transports are possible by adding custom profiles without recompiling.
- **Guide button** (physical Home button on the controller) is mapped - opens Steam overlay,
  Xbox Game Bar, etc.
- **Face-button swap** (`--swap-face-buttons` CLI flag or tray toggle) swaps A/B and X/Y
  for controllers that use the Nintendo layout. The setting persists across restarts.
- **HidHide integration** is optional. Without it, some games may see double input (both
  the raw HID device and the virtual XInput pad), but most modern games handle this
  gracefully. HidHide eliminates the issue entirely.
- Tray icon with status, refresh, startup toggle, and log folder access.
- Runs headless with `--no-tray` for server/CI use.

---

## Quick start

1. Download the latest release zip from [Releases](https://github.com/niftiest/Gamesir-Nova-HD-Rumble-Bluetooth-XInput-Windows/releases) and extract anywhere.
2. Pair the Nova HD with your PC over Bluetooth (see "Pairing the Nova HD" below if you have not done this before).
3. Run `gamesir-nova-hd-xinput.exe`.
4. Launch any XInput game. The controller works.

### Pairing the Nova HD over Bluetooth

The Nova HD must be paired in **Pro Controller mode** (the mode that enables rumble).
Follow these steps exactly:

1. Hold **Home + X** until the LEDs flash, then release.
2. Hold **Home + Screenshot** together until the LEDs are rapidly moving up
   and down. The controller is now broadcasting and discoverable.
3. On the PC, open **Settings -> Bluetooth & devices -> Add device -> Bluetooth**.
   **"Pro Controller"** will appear in the list. Click it to pair.
4. Once paired, the controller stays linked. To power it on for normal use,
   hold the **Home button**. It will reconnect automatically as long as
   Bluetooth is on.

### First run / setup

On first launch the app checks for its two dependencies:

**ViGEmBus** (required) - the virtual gamepad kernel driver. If absent, a one-time UAC
prompt installs it. The driver is open-source and signed by the Nefarius/ViGEm project.

**HidHide** (optional but recommended) - hides the raw HID device from other apps so
games only see the virtual XInput pad. If absent, a one-time UAC prompt offers to install
it. You can skip this; see Troubleshooting → Double inputs if you skip and notice issues.

To configure HidHide after install, either:
- Use the tray menu: **Configure HidHide for this app** - prompts UAC once, then HidHide
  remembers the whitelist across reboots.
- Or run: `gamesir-nova-hd-xinput.exe --configure-hidhide` from an elevated prompt.

Both paths are idempotent - running them again is safe.

### SmartScreen warning

This release is **not code-signed** (signing certificates cost ~$200/year and the project
hasn't graduated to that point). On first launch you will see:

> Windows protected your PC

Click **More info → Run anyway**. If your IT/AV blocks this, build from source instead -
see "Building from source" below.

---

## Features

| Feature | Details |
|---|---|
| XInput emulation | Full Xbox 360 button + axis mapping via ViGEmBus |
| Rumble / haptic feedback | Switch Pro-style rumble output via the Pro Controller Bluetooth mode |
| Guide / Home button | Mapped; opens Steam overlay, Xbox Game Bar, etc. |
| Face-button swap | Swaps A/B and X/Y for Nintendo-layout controllers; persists |
| HidHide integration | Optional; hides raw HID from other apps to prevent double input |
| Profile-driven | Add JSON profiles for other HID controllers - no recompile |
| Tray icon | Status, refresh, startup toggle, log folder shortcut |
| Headless mode | `--no-tray` for console / service use |
| Pro Controller mode (v0.2) | Pairs as "Pro Controller" over Bluetooth; enables rumble |

---

## Usage

### Tray menu

| Item | Effect |
|---|---|
| `N device(s) connected` | Status label (no action). |
| `Refresh` | Re-enumerates HID devices and re-reads `profiles/`. Use after editing a profile. |
| `Configure HidHide for this app` | Adds this exe to HidHide's whitelist (UAC once). |
| `Swap face buttons` | Toggles A↔B / X↔Y swap. Persisted to registry. |
| `Start with Windows` | Opt-in toggle. Adds/removes a value under `HKCU\...\Run`. Default off. |
| `Open log folder` | Opens `%LOCALAPPDATA%\gamesir-nova-hd-xinput\` in Explorer. |
| `Quit` | Clean shutdown. |

### CLI flags

```
gamesir-nova-hd-xinput.exe [options]
```

| Flag | Effect |
|---|---|
| `--no-tray` | Run as a foreground console app. Logs mirror to stderr. Ctrl+C to quit. |
| `--no-bootstrap` | Skip ViGEmBus/HidHide install prompts. If a dependency is missing, exits with a manual-install URL. |
| `--no-hidhide` | Skip the HidHide install prompt entirely (silent skip even if absent). |
| `--no-hidhide-config` | Don't auto-add this exe to HidHide's allow-list. Use if you manage HidHide yourself. |
| `--configure-hidhide` | Configure HidHide for this app (whitelist), then exit. |
| `--swap-face-buttons` | Start with face-button swap enabled (A↔B, X↔Y). |
| `--debug` | Enable DEBUG-level logging. Equivalent to env var `NOVAXINPUT_DEBUG=1`. |

---

## Configuration

### Face-button swap

Some controllers use the Nintendo button layout (B is the bottom face button, A is the
right). Enable the swap so the physical labels match XInput conventions:

- **Tray:** click **Swap face buttons** to toggle. The check mark indicates the current state.
- **CLI:** pass `--swap-face-buttons` at launch.

The setting is stored in the registry under `HKCU\Software\gamesir-nova-hd-xinput` and
persists across restarts.

### HidHide

HidHide prevents other applications from seeing the raw Gamesir HID device. Without it,
Steam or a game may see both the raw device and the virtual XInput pad - this is the
"double input" symptom.

To configure:

1. Install HidHide (the bootstrap will offer to do this on first run).
2. Either use the tray menu item **Configure HidHide for this app**, or run:
   ```
   gamesir-nova-hd-xinput.exe --configure-hidhide
   ```
3. UAC prompts once. HidHide then remembers the configuration across reboots.

If you manage HidHide yourself, pass `--no-hidhide-config` to prevent the app from
touching its allow-list.

---

## Troubleshooting

### Controller not detected

- Confirm the Nova HD Rumble is paired and shows as "Pro Controller" in Windows
  Bluetooth settings, with status "Connected" (not just "Paired"). If it isn't,
  follow the "Pairing the Nova HD over Bluetooth" steps above.
- Open the tray and click **Refresh**.
- Check the log folder (`Open log folder` from tray) for errors - look for `ERROR` or `WARN` lines.
- Verify the profile file `profiles/gamesir-nova-hd-bt.json` is present next to the exe.

### Rumble not working

- The controller must be paired in **Pro Controller mode** (see pairing instructions). The older
  "Wireless Gamepad" pairing mode (VID 0x3537) does not support rumble.
- Re-pair the controller following the updated pairing steps if rumble is absent.

### Double inputs / two controllers showing in games

You are seeing both the raw Gamesir HID device and the virtual XInput pad. Fix:

1. Install HidHide if not already installed (run the exe; the bootstrap will offer).
2. Use the tray menu item **Configure HidHide for this app**, or run:
   ```
   gamesir-nova-hd-xinput.exe --configure-hidhide
   ```

If you want to avoid HidHide entirely, most modern games handle it gracefully - check your
game's controller settings to disable "generic HID" or "DirectInput" devices.

### UAC prompt on every launch

The app only needs elevation for the one-time HidHide configure step. After that step is
done, subsequent launches run without UAC. If UAC appears every time, HidHide configuration
may have failed - try running `--configure-hidhide` once more from an elevated prompt.

### ViGEmBus not found after install

Reboot after ViGEmBus install. The kernel driver requires a restart to load on some systems.

---

## How profiles work

Profiles are JSON files in the `profiles/` directory next to the exe. The app loads all
`*.json` files at startup (and on Refresh). Each profile declares a VID/PID/transport and
maps HID report bytes to XInput button/axis fields.

See `profiles/gamesir-nova-hd-bt.json` for a complete worked example, and the
type definitions in `libgamepadprofile/include/gamepad.h` (`profile_t`,
`button_map_t`, `stick_map_t`, `trigger_map_t`, `axis_type_t`) for the
exhaustive field list.

To add support for a new controller, add a JSON profile - no recompile needed. See
"Adding a new controller" below.

---

## Adding a new controller (contributors)

1. Build (or download) `controller-inspector.exe`.
2. Connect your controller. Find its VID/PID:
   ```powershell
   Get-PnpDevice -Class HIDClass -PresentOnly | Where-Object { $_.FriendlyName -match 'game controller' } | Select-Object FriendlyName, InstanceId
   ```
3. Run the inspector and map each button by pressing one at a time:
   ```
   .\controller-inspector.exe 0xVVVV 0xPPPP --transport=bluetooth
   ```
4. Write a profile JSON modeled on `profiles/gamesir-nova-hd-bt.json`.
5. Open a PR with the new profile. The app picks it up at runtime; no recompile needed.

---

## Building from source

Requirements:
- Windows 10/11 (x64)
- Visual Studio 2019+ Build Tools (MSVC + Windows SDK)
- PowerShell 5.1+
- Git

```powershell
git clone --recursive https://github.com/niftiest/Gamesir-Nova-HD-Rumble-Bluetooth-XInput-Windows.git
cd Gamesir-Nova-HD-Rumble-Bluetooth-XInput-Windows
.\Build.ps1 -Configuration RELEASE -Architecture x64 -RunTests
```

Outputs land in `bin\`. The `-RunTests` flag runs the unit test suite after building.

---

## Acknowledgements

This project was inspired by and structurally derived from
**[Stadia Controller software by furqan-ahm](https://github.com/furqan-ahm/stadia-vigem)**.
The source files `tray.c`, `tray.h`, `hid.c`, and `utils.c` originate from that project
(and its predecessor `walkco/stadia-vigem`), adapted for the Gamesir Nova HD Rumble. Many thanks
to furqan-ahm and the stadia-vigem contributors for the clean, readable foundation.

Other dependencies and credits:

- **[ViGEmBus](https://github.com/ViGEm/ViGEmBus)** - the virtual gamepad kernel driver
  (separately installed; MIT/GPL2 dual). Without this nothing works.
- **[ViGEmClient](https://github.com/ViGEm/ViGEmClient)** - userspace IPC library,
  statically linked (MIT).
- **[HidHide](https://github.com/nefarius/HidHide)** - optional HID device hiding
  (MIT). Developed by Nefarius Software Solutions.
- **[cJSON](https://github.com/DaveGamble/cJSON)** - lightweight JSON parser, vendored
  (MIT).
- **[walkco/stadia-vigem](https://github.com/walkco/stadia-vigem)** and
  **[grayver/Mi-ViGEm](https://github.com/grayver/Mi-ViGEm)** - upstream projects from
  which the stadia-vigem lineage descends. Both MIT.

---

## License

MIT - see `LICENSE`.
