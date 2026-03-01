# Code Review: crab_nebula.cpp vs disting NT API Samples

Compared against: gain.cpp, gainCustomUI.cpp, gainMultichannel.cpp, monosynth.cpp, samplePlayer.cpp, sampleStreamer.cpp, wavetableDemo.cpp, fourteen.cpp, flexSeqSwitch.cpp, parameterPageGroupExample.cpp, midiLFO.cpp, explore.cpp, multiple.cpp, microtuningDemo.cpp

---

## 1. Parameter page group collision (BUG)

**Lines 264-274** — The CV Input pages use explicit `group = 1`, but pages with `group = 0` get auto-assigned their page index as the group number. The Formants page at index 1 auto-assigns to group 1, colliding with the CV pages.

```
Page 0 "Synthesis":  group=0 → auto-assigned 0
Page 1 "Formants":   group=0 → auto-assigned 1  ← COLLISION
Page 6 "CV Inputs":  group=1 (explicit)          ← COLLISION
Page 7 "CV Inputs":  group=1 (explicit)          ← COLLISION
Page 8 "CV Inputs":  group=1 (explicit)          ← COLLISION
```

Result: Cursor position is shared between Formants and all 3 CV pages — almost certainly unintended.

The `parameterPageGroupExample.cpp` avoids this by giving ALL pages explicit non-zero groups (1, 1, 2, 3). Fix: change CV pages to `group = 10` or give all pages explicit groups.

---

## 2. Redundant serialization of standard parameters (BUG)

**Lines 1416-1469** — `serialise()`/`deserialise()` manually save and restore `kParamFolder`, `kParamFile`, and `kParamUseSample`. These are standard parameters in `parametersDefault[]` — the host already saves and restores them automatically via the built-in preset system.

The `samplePlayer.cpp` example uses `kParamFolder` and `kParamSample` with `kNT_unitHasStrings`/`kNT_unitConfirm` and has NO serialise/deserialise at all — it relies entirely on the host.

Consequence: During preset load, `parameterChanged` fires twice for these params — once from host restore, once from `deserialise()` calling `NT_setParameterFromUi()`. This could trigger a double WAV load, where the second attempt is blocked by `awaitingCallback` and silently dropped.

---

## 3. Missing `static_assert` for parameter count validation

**Line 472** — `calculateRequirements` uses `kNumParams` directly. All sample code that uses an enum+array pattern (`samplePlayer.cpp:74`) includes:

```cpp
static_assert( kNumParams == ARRAY_SIZE(parameters) );
```

This catches silent mismatches if a parameter is added to the enum but not the array (or vice versa). crab_nebula has no such check.

Additionally, every sample uses `ARRAY_SIZE(parameters)` in `calculateRequirements` rather than a separate enum value:
```cpp
// samples do this:
req.numParameters = ARRAY_SIZE(parameters);
// crab_nebula does this:
req.numParameters = kNumParams;
```

---

## 4. Unused fields wasting DTC memory

**Lines 91, 98-101** — Five fields in `_pulsarDTC` are explicitly marked as unused:

```cpp
bool prevPulseActive;   // "unused, reserved"
float cvPitchOffset;    // "unused legacy fields"
float cvFormantMod;
float cvDutyMod;
float cvMaskMod;
```

This wastes ~17 bytes of tightly-coupled memory (single-cycle access, limited supply). No sample code keeps dead fields in DTC. Remove them.

---

## 5. Division-by-zero fallback inconsistency between `draw()` and `step()`

**Line 1348 (draw):**
```cpp
pThis->formantHz[0] / (dtc->fundamentalHz > 0.1f ? dtc->fundamentalHz : 100.0f)
```

**Line 1146 (step):**
```cpp
1.0f / (dtc->fundamentalHz > 0.1f ? dtc->fundamentalHz : 0.1f)
```

The draw fallback uses 100.0 Hz while the audio fallback uses 0.1 Hz. When fundamental is between 0.1 and 100 Hz, the waveform preview shows a completely different formant ratio than what's actually synthesized.

---

## 6. `__attribute__((optimize("O2")))` on `step()` — no precedent in samples

**Line 943** — None of the 14 sample plugins use per-function optimization attributes. The Makefile already compiles with `-Os`. Mixing optimization levels within a translation unit can cause subtle issues with inlining decisions and is not a pattern endorsed by the API examples. If more optimization is needed, consider compiling the entire file with `-O2` via the Makefile.

---

## 7. Redundant zero-initialization after `memset`

**Lines 500-527** — After `memset(dtc, 0, sizeof(_pulsarDTC))`, 17 fields are explicitly set to 0/0.0f/false. The `memset` already zeroed them. Only non-zero initializations are needed afterward (`attackCoeff`, `releaseCoeff`, `prngState`, `leakDC_coeff`, `maskSmoothCoeff`, `formantDuty`, `maskSmooth`, `maskTarget`).

---

## 8. `sampleLoadedFrames` set before async load completes

**Lines 744-746** — `sampleLoadedFrames` is set from `info.numFrames` before `NT_readSampleFrames()` is called. If the async load fails (the `success` parameter in `wavCallback` is ignored), `sampleLoadedFrames` incorrectly indicates valid data. The `samplePlayer.cpp` example avoids this by not tracking loaded frame count separately.

---

## 9. Serialization ordering can prevent WAV load on deserialize

**Lines 739-753** — `parameterChanged(kParamFile)` checks `pThis->useSample` before loading:
```cpp
if (!pThis->awaitingCallback && pThis->useSample)
```

In `deserialise()`, members are processed in JSON order: `sampleFolder` → `sampleFile` → `useSample`. When `kParamFile` is restored, `useSample` is still 0 (default), so the WAV load is skipped. This is moot if issue #2 (redundant serialization) is fixed, since the host will restore `useSample` before calling `deserialise()`.

---

## 10. C-style casts instead of `static_cast`

**Lines 462, 492-493, 588, 633-634, 807-808, 945-947** — All casts use C-style: `(_pulsarAlgorithm*)self`, `(_pulsarDTC*)ptrs.dtc`, etc. The `flexSeqSwitch.cpp` example uses `static_cast<>()` consistently. C-style casts bypass type safety checks that `static_cast` provides.

---

## 11. `monosynth.cpp` uses `exp2()` (double), crab_nebula uses `exp2f()` (float) — this is correct

**Line 845** — crab_nebula uses `exp2f()` which is single-precision. The Cortex-M7 FPv5-D16 FPU only accelerates single-precision float ops. The `monosynth.cpp` example actually uses `exp2()` (double-precision, line 92), which requires slow software emulation. crab_nebula is better here — not an issue, just noting the divergence.

---

## 12. `floorf()` in the hot per-sample loop

**Line 1247:**
```cpp
tablePhase -= floorf(tablePhase);
```

This calls the `floorf` library function per sample per formant (up to 3x per sample). Since `tablePhase` is always non-negative, this could be replaced with a cheaper integer truncation:
```cpp
tablePhase -= (float)(int)tablePhase;
```

No sample code has this specific pattern for comparison, but the `monosynth.cpp` inner loop avoids library function calls entirely.

---

## 13. `coeffFromMs()` calls `expf()` per audio block when Glide CV is connected

**Line 1086** — When `cvGlide` is connected, `coeffFromMs(glideMs, sr)` is called every audio block, which invokes `expf()`. On Cortex-M7 without hardware transcendental support, `expf` is a software function (~50-100 cycles). This is per-block not per-sample, so it's acceptable, but worth noting for optimization if block sizes are small.

---

## 14. Potential NaN if `numFramesBy4` is zero

**Line 1050:**
```cpp
float invNumFrames = 1.0f / (float)numFrames;
```

If `numFramesBy4 == 0`, then `numFrames == 0` and `invNumFrames` becomes `+inf`. The CV average sums would be 0.0, and `0.0f * inf = NaN`. No sample code guards against this either, and the host likely never passes 0, but a `numFrames < 1` early return would be defensive.

---

## 15. Factory struct uses explicit NULL for all unused fields

**Lines 1551-1576** — crab_nebula explicitly lists every field in `_NT_factory` including NULLs:
```cpp
.calculateStaticRequirements = NULL,
.initialise = NULL,
.midiRealtime = NULL,
.midiSysEx = NULL,
.parameterUiPrefix = NULL,
```

The sample code (e.g., `monosynth.cpp:129-141`, `gainCustomUI.cpp:240-257`) omits unused fields entirely, relying on C++ designated initializer zero-initialization. Both approaches work, but the samples' approach is more concise and less prone to maintenance issues when fields are added to the struct.

---

## Summary by severity

| # | Severity | Issue |
|---|----------|-------|
| 1 | Bug | Page group collision — Formants and CV pages share group 1 |
| 2 | Bug | Redundant serialization triggers double parameterChanged |
| 9 | Bug | Serialization order prevents WAV load (moot if #2 fixed) |
| 3 | Defect | Missing static_assert for param count validation |
| 4 | Waste | Unused DTC fields waste 17 bytes of tightly-coupled RAM |
| 5 | Defect | draw() vs step() fallback Hz inconsistency |
| 8 | Defect | sampleLoadedFrames set before async load confirms success |
| 6 | Style | `__attribute__((optimize))` has no precedent in samples |
| 7 | Style | Redundant zero-init after memset |
| 10 | Style | C-style casts vs static_cast |
| 12 | Perf | `floorf()` in hot loop |
| 13 | Perf | `expf()` per block for glide CV |
| 14 | Defensive | Potential NaN on zero-length block |
| 15 | Style | Verbose factory NULL fields |
