BioPatch is a standalone `.asi` plugin for Resident Evil Revelations 2.

It focuses on replacing legacy code with cleaner modern behavior.

Nexus mods: https://www.nexusmods.com/residentevilrevelations2/mods/144

## What BioPatch does

- Timing cleanup:
  - replaces legacy `timeGetTime` reads with a high precision QPC-backed timer
  - replaces validated short `Sleep` / `SleepEx` timing paths with more precise waitable-timer behavior
  - replaces several traced `Sleep(1)` polling loops with direct waits, message waits, yields, or adaptive backoff where that was proven safe
- Input cleanup:
  - removes redundant per-frame `ClipCursor`, `GetCursorPos`, `ScreenToClient`, `ClientToScreen`, and `GetClientRect` traffic from the traced fullscreen input path
  - disables the traced QFPS mouse-control lag on the validated live/default camera objects
- Stability and support:
  - writes a runtime log
  - writes crash dumps on unhandled crashes
  - protects BioPatch's crash handler if the game tries to replace it

## Config

- Main config file: `BioPatch.ini`
- `1` = on, `0` = off
- Recommended everyday settings are already enabled
- Probe, dump, and niche options stay off by default
- The config is split into:
  - `General`
  - `Timing`
  - `Input`
  - `Optional`
  - `Diagnostics`
- Older flat `[MAIN]` configs still load for backward compatibility

## Build

```bat
build.bat
```

## Deploy

```bat
deploy.bat
```

## Output

- `build\BioPatch.asi`
- `build\BioPatch.pdb` when built with debug info

## Runtime Files

- `scripts\BioPatch\BioPatch.log`
- `scripts\BioPatch\crashdumps\*.dmp`
