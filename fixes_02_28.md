# Fixes Applied: 2/28 Code Review

All issues from `review_02_28.md` addressed, plus output path fixes.

## Bugs

| # | Issue | Fix |
|---|-------|-----|
| 1 | Page group collision — Formants (auto-assigned group 1) collided with CV Input pages (explicit group 1) | All pages now have explicit non-zero groups: 1–6 for single pages, 10 for CV Inputs, 11 for Routing |
| 2 | Redundant serialization — `serialise()`/`deserialise()` duplicated host's built-in preset save/restore, causing double `parameterChanged` calls on preset load | Removed both functions and `#include <distingnt/serialisation.h>` |
| 8 | `sampleLoadedFrames` set before async WAV load completed — incorrect count on failure | Set to 0 before load starts; set to correct count in `wavCallback` only on success |
| 9 | Serialization ordering prevented WAV load on deserialize (`useSample` was still 0 when `kParamFile` restored) | Moot with #2 fixed; also removed `useSample` guard from `kParamFile` handler to match `samplePlayer.cpp` pattern |

## Defects

| # | Issue | Fix |
|---|-------|-----|
| 3 | Missing `static_assert` for parameter count validation | Added `static_assert(kNumParams == ARRAY_SIZE(parametersDefault))` and use `ARRAY_SIZE(parametersDefault)` in `calculateRequirements` |
| 5 | `draw()` division-by-zero fallback used 100.0 Hz vs `step()` using 0.1 Hz | Changed `draw()` fallback to 0.1 Hz to match `step()` |

## Performance

| # | Issue | Fix |
|---|-------|-----|
| 4 | 5 unused fields in DTC wasting 17 bytes of tightly-coupled RAM | Removed `prevPulseActive`, `cvPitchOffset`, `cvFormantMod`, `cvDutyMod`, `cvMaskMod` |
| 12 | `floorf()` library call in hot per-sample loop | Replaced with integer truncation: `(float)(int)tablePhase` |
| 14 | Potential NaN if `numFramesBy4` is zero | Added `if (numFrames < 1) return` early exit in `step()` |

## Style

| # | Issue | Fix |
|---|-------|-----|
| 6 | `__attribute__((optimize("O2")))` on `step()` — no precedent in API samples | Removed |
| 7 | 17 redundant zero-initializations after `memset(dtc, 0, ...)` | Removed; only non-zero inits remain |
| 10 | C-style casts throughout | Replaced with `static_cast<>()` / `reinterpret_cast<>()` |
| 15 | Factory struct listed explicit NULLs for unused fields | Removed; relies on C++ designated initializer zero-init (matches API samples) |

## Output Path Fix

| Issue | Fix |
|-------|-----|
| Amplitude CV defaulted to bus 12 — floating input reads ~0V, zeroing `ampCvMul` and silencing all output | Changed default to bus 0 (none); `ampCvMul` stays 1.0 unless user explicitly routes a CV source |
| No way to verify synthesis output on hardware | Added peak level meter bar below waveform display |

## Not Changed (acceptable as-is per review)

| # | Note |
|---|------|
| 11 | `exp2f()` (float) is correct for Cortex-M7 — better than API sample's `exp2()` (double) |
| 13 | `expf()` per block for glide CV is acceptable at block rate |
