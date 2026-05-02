BioPatch
Modernization patch for Resident Evil Revelations 2

BioPatch is a standalone ASI mod that replaces legacy code paths with cleaner modern behavior.
It focuses on timing, polling, and input overhead.

Features

- High precision replacement for the game's legacy timeGetTime usage.
- More precise handling for several validated short Sleep and SleepEx timing paths.
- Replaces multiple traced Sleep(1) polling loops with direct waits, message waits, yields, or adaptive backoff where that was proven safe.
- Removes redundant fullscreen input traffic such as repeated ClipCursor, GetCursorPos, ScreenToClient, ClientToScreen, and GetClientRect calls.
- Disables the traced QFPS mouse-control lag on validated live/default camera objects.
- Writes a runtime log and crash dumps for troubleshooting.

Files

- BioPatch.asi
- BioPatch.ini

Installation

1. Copy BioPatch.asi and BioPatch.ini into:
   Resident Evil Revelations 2\scripts
2. Launch the game normally through Steam.

Config

- BioPatch.ini is fully commented.
- Recommended settings are already enabled by default.
- Optional diagnostics and niche toggles stay off by default.

Runtime output

- scripts\BioPatch\BioPatch.log
- scripts\BioPatch\crashdumps\*.dmp
