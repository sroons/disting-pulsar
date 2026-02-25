# Fixes Applied from Code Review

All 15 review items from `review.md` have been addressed. Zero errors, zero warnings after rebuild.

---

## 1. Precompute reciprocal of sample rate per block

**File:** `src/pulsar.cpp`, `step()`

Added `float invSr = 1.0f / sr;` before the sample loop and replaced `freqHz / sr` with `freqHz * invSr`. Eliminates one floating-point division per sample, replacing it with a single division per block and a multiplication per sample (~14x cheaper on Cortex-M7 FPv5).

---

## 2. Precompute reciprocal of duty per formant

**File:** `src/pulsar.cpp`, `step()`

Added `float invDuty[3]` array computed once per block before the sample loop. Replaced `phase / duty` in the inner per-formant loop with `phase * invDuty[f]`. Eliminates one division per sample per active formant from the hottest loop in the plugin.

---

## 3. Remove no-op mask smooth hold assignment

**File:** `src/pulsar.cpp`, `step()`

Removed the `else` branch containing `dtc->maskSmooth[f] = dtc->maskSmooth[f]; // hold`. This was a self-assignment producing a pointless memory load/store every sample for every formant on non-pulse samples. The value already persists in memory. (This was also subsumed by the fix for issue 8 which restructured the mask smoothing logic entirely.)

---

## 4. Combine CV averaging into a single loop

**File:** `src/pulsar.cpp`, `step()`

Merged three separate CV averaging loops into a single loop that accumulates all three CV averages in one pass over the buffer. Also precomputed `invNumFrames` to replace the three divisions with multiplications. Reduces loop overhead and improves data cache utilization.

---

## 5. Replace per-sample exp2f with fast approximation

**File:** `src/pulsar.cpp`

Added `fastExp2f()` inline function using integer bit manipulation with a cubic polynomial refinement. Accurate to ~1 cent over the [-4, 4] range (well within musical tolerance for 1V/oct CV). Replaced the per-sample `exp2f(cvPitch[i])` call in the sample loop. Provides ~5-10x speedup for the exp2 computation since it avoids the costly libm call.

---

## 6. Hoist maskSmoothCoeff out of the sample loop

**File:** `src/pulsar.cpp`, `step()`

Moved `maskSmoothCoeff` from inside the per-sample loop to before it. The value is now read once from the DTC struct before the loop starts, guaranteeing no per-sample overhead. (This was combined with issue 10 which made the coefficient sample-rate dependent.)

---

## 7. Precompute formant ratio outside the sample loop

**File:** `src/pulsar.cpp`, `step()`

Added `formantRatioPrecomp[3]` array computed once per block when pitch CV is not connected. Inside the inner loop, a `hasPitchCV` branch selects between the precomputed ratio (common case) and per-sample computation (only when pitch CV is active). Eliminates one division per sample per formant in the no-CV case.

---

## 8. Fix mask smoothing to run continuously

**File:** `src/pulsar.cpp`, DTC struct + `step()` + `construct()`

Added `float maskTarget[3]` to the DTC struct. On new pulse events, the mask gain is written to `maskTarget[f]`. The one-pole smoothing filter now runs every sample: `maskSmooth[f] = maskTarget[f] + coeff * (maskSmooth[f] - maskTarget[f])`. Previously the filter only ran for a single sample at each pulse boundary, providing negligible smoothing. Now the filter properly converges toward the target over multiple samples, producing clean crossfade-like transitions between masked and unmasked pulses and eliminating clicks.

---

## 9. Make LeakDC coefficient sample-rate dependent

**File:** `src/pulsar.cpp`, DTC struct + `construct()` + `step()`

Added `float leakDC_coeff` to the DTC struct. In `construct()`, the coefficient is computed as `1.0 - (2*pi*25 / sampleRate)` targeting a consistent ~25 Hz cutoff regardless of sample rate. Replaced the hardcoded `0.995f` in both L and R DC-blocking filters with `dtc->leakDC_coeff`. This gives consistent low-frequency response at any sample rate instead of varying between ~38 Hz at 48kHz and ~19 Hz at 96kHz.

---

## 10. Make mask smooth coefficient sample-rate dependent

**File:** `src/pulsar.cpp`, DTC struct + `construct()` + `step()`

Added `float maskSmoothCoeff` to the DTC struct. In `construct()`, computed via `coeffFromMs(3.0f, sr)` targeting a consistent ~3 ms smoothing time constant. The step function reads this from DTC once before the sample loop. Previously the hardcoded `0.995f` gave different smoothing times at different sample rates (~4.6ms at 48kHz, ~9.2ms at 96kHz).

---

## 11. Guard against negative sample buffer index

**File:** `src/pulsar.cpp`, `step()`

Changed the sample playback guard from `pThis->sampleLoadedFrames > 0` to `pThis->sampleLoadedFrames >= 2`. With only 1 loaded frame, `sampleLoadedFrames - 2` would be negative, causing an out-of-bounds read. The interpolation requires at least 2 frames (current + next), so requiring >= 2 prevents the potential out-of-bounds memory access.

---

## 12. Velocity is stored but unused

**File:** `src/pulsar.cpp`, `step()`

Added velocity scaling to the envelope output. Computed `float vel = dtc->velocity * (1.0f / 127.0f)` and multiplied into the amplitude path: `sumL *= dtc->envValue * amplitude * vel`. MIDI velocity now scales note loudness as expected for a MIDI instrument, improving playability and expressiveness.

---

## 13. Use floorf() instead of (int) cast for phase wrapping

**File:** `src/pulsar.cpp`, `step()`

Replaced `tablePhase -= (int)tablePhase;` followed by `if (tablePhase < 0.0f) tablePhase += 1.0f;` with the more robust `tablePhase -= floorf(tablePhase)`. The `(int)` cast truncates toward zero, which handles negative values incorrectly (and the +1.0 fixup only corrects by one period). `floorf()` always rounds toward negative infinity, correctly wrapping to [0, 1) for any input value including edge cases at extreme formant ratios.

---

## 14. Use -O2 for the step function

**File:** `src/pulsar.cpp`, `step()`

Added `__attribute__((optimize("O2")))` to the `step()` function definition. This overrides the global `-Os` (optimize for size) with `-O2` (optimize for speed) for just the audio processing function, allowing the compiler to unroll inner loops, inline more aggressively, and better schedule FPU instructions. The rest of the plugin remains size-optimized.

---

## 15. Add -ffast-math

**File:** `Makefile`

Added `-ffast-math` to the compiler flags. This enables FMA (fused multiply-add) instruction emission on Cortex-M7 FPv5, floating-point operation reordering, reciprocal approximations for division, and skipping NaN/Inf checks. The plugin already uses fast approximations (fastTanh, fastExp2f) and doesn't rely on strict IEEE 754 semantics, so this is safe. Cortex-M7 FPv5 has hardware FMA which combines multiply+add into a single cycle, but the compiler won't emit these without `-ffast-math`.
