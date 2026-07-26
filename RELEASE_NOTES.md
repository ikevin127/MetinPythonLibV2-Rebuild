# eXLib — GF 26.1.11 release notes

Re-port of eXLib to the auto-updated GameForge client (**26.0.7 → 26.1.11**). Rebuilt with MSVC v142 → `build/eXLib.dll`, deployed as `eXLib.mix`.

## Launch-crash fixes — `common/Patterns.cpp`
- The AOB scanner scanned the whole 0..2 GB address space to locate the host module → overread across region boundaries → `0xC0000005`. Now bounds to the host exe directly (`GetModuleHandle(NULL)` + PE `SizeOfImage`).
- The region-skip branch used a running total (`pageEndAddr += RegionSize`) → desync → read of an unmapped page. Now `pageEndAddr = BaseAddress + RegionSize`.

## World-reload / teleport use-after-free — `main.cpp`, `App.cpp`
- **Cause:** `CPythonNetworkStream::SetPhaseWindow` stores the game window **borrowed** (no `Py_INCREF`); uBot's object churn drops its last Python ref while the client still calls methods on the dangling pointer → getattr on a freed object → crash.
- **`main.cpp`:** a vectored exception handler (`exVehHandler`) — not a debugger, since the client's anti-debug crashes on breakpoints. On the exact fault it writes `exlib_crash.txt` (unbuffered) and **recovers** (return NULL + resume at the fn epilogue) so the client survives.
- **`App.cpp` `pinGameWindow()`:** the root fix — holds one ref to the live `m_apoPhaseWnd[GAME]` so the borrowed pointer can never reach refcount 0. Sources the singleton from the **live captured stream** (`CMemory::getNetworkStream()`); the old hardcoded net-global RVA went stale on this build.
- **`exlib_toggles.txt`:** runtime toggles to bisect subsystems live without a rebuild.

## Position offset — `defines.h`
- `OFFSET_CLIENT_CHARACTER_POS` **`0x7BC` → `0x7C4`** (the instance struct grew on 26.1.11). This is what `eXLib.GetPixelPosition` reads; a wrong value returns NaN and silently breaks every position-dependent bot.

## NPC dialog option count — `Memory.cpp/.h`, `PythonModule.cpp`
- `GetDialogAnswerCount()` — walks the `CPythonEventManager` singleton's `m_EventSetVector` and returns the current dialog's option count (for EnergyBot's "pick second-to-last / Open Shop").
- **Known issue:** the event-manager singleton's static-global RVA is **stale on the current 26.1.11 build** (needs re-derive). Until then the call returns garbage and **EnergyBot falls back to its "I have Weapon Shop mission" checkbox**, which works.

## Supporting
- Self-contained `PythonUtils` method-call helpers; `Player`, `NetworkStream`, `Background`, `MapCollision` updates for the 26.1.11 re-port.

Pairs with `../uBot-WalkerPath` (deployed there as `eXLib.mix`).
