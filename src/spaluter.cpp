// ============================================================
// Pulsar Synthesis — disting NT Plugin
// ============================================================
//
// A MIDI-controlled pulsar synthesis instrument based on Curtis Roads'
// technique: trains of short sonic particles (pulsarets) are generated
// at a fundamental frequency, each shaped by a window function. Up to
// 3 parallel formants with independent frequency, stereo panning, and
// stochastic or burst masking create rich, evolving timbres.
//
// 4-voice polyphony: MIDI mode plays chords with voice stealing,
// Free Run mode stacks harmonic intervals (octaves, fifths, etc.).
//
// Architecture:
//   DRAM  (~312 KB) — pre-computed pulsaret/window lookup tables + sample buffer
//   DTC   (~424 B)  — per-sample hot state (4 voices × phase, envelope, DC filter, PRNG)
//   SRAM  (~1 KB)   — algorithm struct, cached params, WAV request state
//
// Signal chain (per sample):
//   For each voice:
//     Master phase oscillator → pulse trigger → mask decision
//     → For each formant: pulsaret × window × mask → constant-power pan
//     → Normalize → envelope × velocity × amplitude
//     → DC-blocking highpass
//   Sum voices → normalize by voice count → Padé tanh soft clip → stereo output
//
// Hardware controls:
//   Pot L = pulsaret morph, Pot C = duty cycle, Pot R = window morph
//   Encoder button L = cycle mask mode, Encoder button R = cycle formant count
//
// ============================================================

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>
#include <new>
#include <distingnt/api.h>
#include <distingnt/wav.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// ============================================================
// Table sizes
// ============================================================
static const int kTableSize = 2048;         // Samples per waveform/window table
static const int kNumPulsarets = 10;        // Number of pulsaret waveforms
static const int kNumWindows = 5;           // Number of window functions
static const int kSampleBufferSize = 48000; // Max sample frames (1 sec at 48kHz)

// ============================================================
// Memory structures
// ============================================================

// DRAM: large pre-computed lookup tables and sample buffer (~312 KB)
struct _pulsarDRAM {
	float pulsaretTables[kNumPulsarets][kTableSize]; // 10 waveforms: sine, sine×2, sine×3, sinc, tri, saw, square, formant, pulse, noise
	float windowTables[kNumWindows][kTableSize];     // 5 windows: rectangular, gaussian, hann, exp decay, linear decay
	float sampleBuffer[kSampleBufferSize];           // WAV sample data for sample-based pulsarets
};

// Per-voice state (~104 bytes each)
struct _pulsarVoice {
	// Master oscillator
	float masterPhase;          // 0.0–1.0 sawtooth phase accumulator
	float fundamentalHz;        // Current fundamental frequency (smoothed by glide)
	float targetFundamentalHz;  // Target frequency from MIDI note or interval
	float glideCoeff;           // One-pole glide/portamento coefficient

	// Per-formant state
	float formantDuty[3];       // Duty cycle per formant (ratio of pulse that is active)
	float maskSmooth[3];        // Smoothed mask gain per formant (0=muted, 1=sounding)
	float maskTarget[3];        // Mask target per formant (updated on pulse boundaries)
	float maskSmoothCoeff;      // Sample-rate-dependent mask smoothing coefficient (~3ms)

	// ASR envelope
	float envValue;             // Current envelope level (0.0–1.0)
	float envTarget;            // Envelope target (1.0 when gate on, 0.0 when off)
	float attackCoeff;          // One-pole attack coefficient
	float releaseCoeff;         // One-pole release coefficient

	// DC-blocking highpass filter state (y = x - x_prev + coeff * y_prev)
	float leakDC_xL;            // Previous input sample, left channel
	float leakDC_yL;            // Previous output sample, left channel
	float leakDC_xR;            // Previous input sample, right channel
	float leakDC_yR;            // Previous output sample, right channel
	float leakDC_coeff;         // Sample-rate-dependent coefficient (~25 Hz cutoff)

	// MIDI state
	uint8_t currentNote;        // Currently held MIDI note number
	uint8_t velocity;           // Note-on velocity (0–127), scales output amplitude
	bool gate;                  // True while a note is held

	// Masking state
	uint32_t prngState;         // LCG pseudo-random number generator state
	uint32_t burstCounter;      // Burst pattern counter (modulo burstOn+burstOff)
};

// DTC: performance-critical per-sample audio state (~424 bytes)
// Lives in Cortex-M7 tightly-coupled memory for single-cycle access.
static const int kMaxVoices = 4;

struct _pulsarDTC {
	_pulsarVoice voices[kMaxVoices]; // 4 voice slots (~416 bytes)
	uint8_t voiceAge[kMaxVoices];    // LRU tracking for voice stealing
	uint8_t nextVoiceAge;            // Monotonic counter for age assignment
};

// ============================================================
// Parameter indices
//
// 44 parameters across 12 pages. Indices must match the order
// of entries in the parametersDefault[] array below.
// ============================================================

enum {
	// -- Synthesis page --
	kParamPulsaret,     // 0.0–9.0 (scaling10): morphs between 10 pulsaret waveforms
	kParamWindow,       // 0.0–4.0 (scaling10): morphs between 5 window functions
	kParamDutyCycle,    // 1–100%: fraction of pulse period containing active pulsaret
	kParamDutyMode,     // Enum: Manual (use Duty Cycle param) or Formant (auto-derive from freq ratio)

	// -- Formants page --
	kParamFormantCount, // 1–3: number of parallel formant oscillators
	kParamFormant1Hz,   // 20–8000 Hz: formant 1 frequency
	kParamFormant2Hz,   // 20–8000 Hz: formant 2 frequency (grayed when count < 2)
	kParamFormant3Hz,   // 20–8000 Hz: formant 3 frequency (grayed when count < 3)

	// -- Masking page --
	kParamMaskMode,     // Enum: Off / Stochastic (random) / Burst (periodic pattern)
	kParamMaskAmount,   // 0–100%: probability of muting a pulse (stochastic mode)
	kParamBurstOn,      // 1–16: number of consecutive sounding pulses (burst mode)
	kParamBurstOff,     // 0–16: number of consecutive muted pulses (burst mode)

	// -- Envelope page --
	kParamAttack,       // 0.1–2000 ms (scaling10): ASR envelope attack time
	kParamRelease,      // 1.0–3200 ms (scaling10): ASR envelope release time
	kParamAmplitude,    // 0–100%: master output amplitude
	kParamGlide,        // 0–2000 ms (scaling10): portamento time between notes

	// -- Panning page --
	kParamPan1,         // -100 to +100: stereo pan for formant 1 (constant-power)
	kParamPan2,         // -100 to +100: stereo pan for formant 2 (grayed when count < 2)
	kParamPan3,         // -100 to +100: stereo pan for formant 3 (grayed when count < 3)

	// -- Sample page --
	kParamUseSample,    // Enum: Off/On — replaces synthesized pulsaret with WAV sample
	kParamFolder,       // SD card sample folder selector (kNT_unitHasStrings)
	kParamFile,         // SD card sample file selector (kNT_unitConfirm, triggers async load)
	kParamSampleRate,   // 25–400%: playback rate multiplier for sample pulsaret

	// -- CV Inputs page 1 --
	kParamPitchCV,      // Bus selector: 1V/oct pitch modulation (per-sample)
	kParamDutyCV,       // Bus selector: bipolar ±5V → ±20% duty offset
	kParamMaskCV,       // Bus selector: bipolar ±5V → ±50% mask amount offset

	// -- CV Inputs page 2 --
	kParamPulsaretCV,   // Bus selector: bipolar ±5V → full range sweep
	kParamWindowCV,     // Bus selector: bipolar ±5V → full range sweep
	kParamAmplitudeCV,  // Bus selector: bipolar ±5V → ±50% amplitude offset

	// -- CV Inputs page 3 --
	kParamFormant1CV,   // Bus selector: bipolar ±5V → ±4000 Hz offset
	kParamFormant2CV,   // Bus selector: bipolar ±5V → ±4000 Hz offset
	kParamFormant3CV,   // Bus selector: bipolar ±5V → ±4000 Hz offset

	// -- CV Inputs page 4 --
	kParamPan1CV,       // Bus selector: bipolar ±5V → ±100% pan offset
	kParamAttackCV,     // Bus selector: bipolar ±5V → ±1000 ms attack offset
	kParamReleaseCV,    // Bus selector: bipolar ±5V → ±1600 ms release offset

	// -- Routing page --
	kParamMidiCh,       // 1–16: MIDI channel filter
	kParamOutputL,      // Bus selector: left audio output
	kParamOutputLMode,  // Output mode: 0=add, 1=replace
	kParamOutputR,      // Bus selector: right audio output
	kParamOutputRMode,  // Output mode: 0=add, 1=replace

	kParamGateMode,     // Enum: MIDI / Free Run
	kParamBasePitch,    // MIDI note 0-127, default 69 (A4)

	// -- Polyphony page --
	kParamVoiceCount,   // 1–4: number of simultaneous voices
	kParamChordType,    // Enum: chord/interval type for Free Run mode

	kNumParams,
};

// ============================================================
// Enum strings
// ============================================================

static char const * const enumDutyMode[] = { "Manual", "Formant" };
static char const * const enumMaskMode[] = { "Off", "Stochastic", "Burst" };
static char const * const enumUseSample[] = { "Off", "On" };
static char const * const enumGateMode[] = { "MIDI", "Free Run" };
static char const * const enumChordType[] = {
	"Unison", "Octaves", "Fifths", "Sub+Oct",
	"Major", "Minor", "Maj7", "Min7",
	"Sus4", "Dom7", "Dim", "Aug",
	"Power", "Open5th"
};

// ============================================================
// Chord/interval ratio tables for Free Run polyphony
//
// Harmonic entries use exact ratios. Tonal chords use equal-
// temperament semitone ratios: 2^(st/12).
// ============================================================

#define ST(n) (1.0f)  // placeholder — filled by initChordRatios()

static const int kNumChordTypes = 14;
static float chordRatios[kNumChordTypes][kMaxVoices];

// Called once from construct() to fill the ratio table
static void initChordRatios()
{
	// Helper: semitone → frequency ratio
	// Can't use constexpr with powf, so we compute at init time
	auto st = [](int semitones) -> float {
		return powf(2.0f, semitones / 12.0f);
	};

	// Harmonic ratios
	float table[][kMaxVoices] = {
		{ 1.0f, 1.0f,   1.0f,   1.0f   },  // 0: Unison
		{ 1.0f, 2.0f,   4.0f,   8.0f   },  // 1: Octaves
		{ 1.0f, 1.5f,   2.0f,   3.0f   },  // 2: Fifths
		{ 0.5f, 1.0f,   2.0f,   4.0f   },  // 3: Sub+Oct
	};
	for (int i = 0; i < 4; ++i)
		for (int v = 0; v < kMaxVoices; ++v)
			chordRatios[i][v] = table[i][v];

	// Tonal chords (semitone intervals from root)
	int chords[][kMaxVoices] = {
		{ 0, 4, 7, 12 },  // 4: Major
		{ 0, 3, 7, 12 },  // 5: Minor
		{ 0, 4, 7, 11 },  // 6: Maj7
		{ 0, 3, 7, 10 },  // 7: Min7
		{ 0, 5, 7, 12 },  // 8: Sus4
		{ 0, 4, 7, 10 },  // 9: Dom7
		{ 0, 3, 6, 12 },  // 10: Dim
		{ 0, 4, 8, 12 },  // 11: Aug
		{ 0, 7, 12, 19 }, // 12: Power
		{ 0, 7, 12, 16 }, // 13: Open5th
	};
	for (int i = 0; i < 10; ++i)
		for (int v = 0; v < kMaxVoices; ++v)
			chordRatios[4 + i][v] = st(chords[i][v]);
}

#undef ST

// ============================================================
// Parameter definitions
// ============================================================

static const _NT_parameter parametersDefault[] = {
	// Synthesis page
	{ .name = "Pulsaret",    .min = 0,   .max = 90,    .def = 25,  .unit = kNT_unitNone,    .scaling = kNT_scaling10, .enumStrings = NULL },
	{ .name = "Window",      .min = 0,   .max = 40,    .def = 5,   .unit = kNT_unitNone,    .scaling = kNT_scaling10, .enumStrings = NULL },
	{ .name = "Duty Cycle",  .min = 1,   .max = 100,   .def = 50,  .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = NULL },
	{ .name = "Duty Mode",   .min = 0,   .max = 1,     .def = 0,   .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = enumDutyMode },

	// Formants page
	{ .name = "Formant Count", .min = 1,  .max = 3,    .def = 2,   .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = NULL },
	{ .name = "Formant 1 Hz",  .min = 20, .max = 2000, .def = 20,  .unit = kNT_unitHz,      .scaling = kNT_scalingNone, .enumStrings = NULL },
	{ .name = "Formant 2 Hz",  .min = 20, .max = 2000, .def = 200, .unit = kNT_unitHz,      .scaling = kNT_scalingNone, .enumStrings = NULL },
	{ .name = "Formant 3 Hz",  .min = 20, .max = 2000, .def = 400, .unit = kNT_unitHz,      .scaling = kNT_scalingNone, .enumStrings = NULL },

	// Masking page
	{ .name = "Mask Mode",   .min = 0,   .max = 2,     .def = 0,   .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = enumMaskMode },
	{ .name = "Mask Amount", .min = 0,   .max = 100,   .def = 50,  .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = NULL },
	{ .name = "Burst On",    .min = 1,   .max = 16,    .def = 4,   .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = NULL },
	{ .name = "Burst Off",   .min = 0,   .max = 16,    .def = 4,   .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = NULL },

	// Envelope page
	{ .name = "Attack",      .min = 1,   .max = 20000, .def = 100, .unit = kNT_unitMs,      .scaling = kNT_scaling10, .enumStrings = NULL },
	{ .name = "Release",     .min = 10,  .max = 32000, .def = 2000,.unit = kNT_unitMs,      .scaling = kNT_scaling10, .enumStrings = NULL },
	{ .name = "Amplitude",   .min = 0,   .max = 100,   .def = 0,   .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = NULL },
	{ .name = "Glide",       .min = 0,   .max = 20000, .def = 0,   .unit = kNT_unitMs,      .scaling = kNT_scaling10, .enumStrings = NULL },

	// Panning page
	{ .name = "Pan 1",       .min = -100,.max = 100,   .def = 0,   .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = NULL },
	{ .name = "Pan 2",       .min = -100,.max = 100,   .def = -50, .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = NULL },
	{ .name = "Pan 3",       .min = -100,.max = 100,   .def = 50,  .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = NULL },

	// Sample page
	{ .name = "Use Sample",  .min = 0,   .max = 1,     .def = 0,   .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = enumUseSample },
	{ .name = "Folder",      .min = 0,   .max = 32767, .def = 0,   .unit = kNT_unitHasStrings, .scaling = kNT_scalingNone, .enumStrings = NULL },
	{ .name = "File",        .min = 0,   .max = 32767, .def = 0,   .unit = kNT_unitConfirm,    .scaling = kNT_scalingNone, .enumStrings = NULL },
	{ .name = "Sample Rate", .min = 25,  .max = 400,   .def = 100, .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = NULL },

	// CV Inputs page 1
	NT_PARAMETER_CV_INPUT( "Pitch CV",       0, 1 )
	NT_PARAMETER_CV_INPUT( "Duty CV",        0, 3 )
	NT_PARAMETER_CV_INPUT( "Mask CV",        0, 4 )

	// CV Inputs page 2
	NT_PARAMETER_CV_INPUT( "Pulsaret CV",    0, 5 )
	NT_PARAMETER_CV_INPUT( "Window CV",      0, 6 )
	NT_PARAMETER_CV_INPUT( "Amplitude CV",   0, 2 )

	// CV Inputs page 3
	NT_PARAMETER_CV_INPUT( "Formant 1 CV",   0, 7 )
	NT_PARAMETER_CV_INPUT( "Formant 2 CV",   0, 8 )
	NT_PARAMETER_CV_INPUT( "Formant 3 CV",   0, 9 )

	// CV Inputs page 4
	NT_PARAMETER_CV_INPUT( "Pan 1 CV",       0, 10 )
	NT_PARAMETER_CV_INPUT( "Attack CV",      0, 11 )
	NT_PARAMETER_CV_INPUT( "Release CV",     0, 12 )

	// Routing page
	{ .name = "MIDI Ch",     .min = 1,   .max = 16,    .def = 1,   .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = NULL },
	NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Output L", 1, 13 )
	NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Output R", 1, 14 )

	// Gate mode (must be at end of routing to match enum order)
	{ .name = "Gate Mode",   .min = 0,   .max = 1,     .def = 1,   .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = enumGateMode },
	{ .name = "Base Pitch",  .min = 0,   .max = 127,   .def = 24,  .unit = kNT_unitMIDINote, .scaling = kNT_scalingNone, .enumStrings = NULL },

	// Polyphony page
	{ .name = "Voice Count", .min = 1,   .max = 4,     .def = 1,   .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = NULL },
	{ .name = "Chord Type",  .min = 0,   .max = 13,    .def = 0,   .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = enumChordType },
};

// ============================================================
// Parameter pages
// ============================================================

static const uint8_t pageSynthesis[] = { kParamPulsaret, kParamWindow, kParamDutyCycle, kParamDutyMode };
static const uint8_t pageFormants[]  = { kParamFormantCount, kParamFormant1Hz, kParamFormant2Hz, kParamFormant3Hz };
static const uint8_t pageMasking[]   = { kParamMaskMode, kParamMaskAmount, kParamBurstOn, kParamBurstOff };
static const uint8_t pageEnvelope[]  = { kParamAttack, kParamRelease, kParamAmplitude, kParamGlide };
static const uint8_t pagePanning[]   = { kParamPan1, kParamPan2, kParamPan3 };
static const uint8_t pagePolyphony[] = { kParamVoiceCount, kParamChordType };
static const uint8_t pageSample[]    = { kParamUseSample, kParamFolder, kParamFile, kParamSampleRate };
static const uint8_t pageCV1[]       = { kParamPitchCV, kParamDutyCV, kParamMaskCV };
static const uint8_t pageCV2[]       = { kParamPulsaretCV, kParamWindowCV, kParamAmplitudeCV };
static const uint8_t pageCV3[]       = { kParamFormant1CV, kParamFormant2CV, kParamFormant3CV };
static const uint8_t pageCV4[]       = { kParamPan1CV, kParamAttackCV, kParamReleaseCV };
static const uint8_t pageRouting[]   = { kParamOutputL, kParamOutputLMode, kParamOutputR, kParamOutputRMode, kParamGateMode, kParamMidiCh, kParamBasePitch };

static const _NT_parameterPage pages[] = {
	{ .name = "Synthesis",  .numParams = ARRAY_SIZE(pageSynthesis), .group = 1, .params = pageSynthesis },
	{ .name = "Formants",   .numParams = ARRAY_SIZE(pageFormants),  .group = 2, .params = pageFormants },
	{ .name = "Masking",    .numParams = ARRAY_SIZE(pageMasking),   .group = 3, .params = pageMasking },
	{ .name = "Envelope",   .numParams = ARRAY_SIZE(pageEnvelope),  .group = 4, .params = pageEnvelope },
	{ .name = "Panning",    .numParams = ARRAY_SIZE(pagePanning),   .group = 5, .params = pagePanning },
	{ .name = "Polyphony",  .numParams = ARRAY_SIZE(pagePolyphony), .group = 7, .params = pagePolyphony },
	{ .name = "Sample",     .numParams = ARRAY_SIZE(pageSample),    .group = 6, .params = pageSample },
	{ .name = "CV Inputs",  .numParams = ARRAY_SIZE(pageCV1),       .group = 10, .params = pageCV1 },
	{ .name = "CV Inputs",  .numParams = ARRAY_SIZE(pageCV2),       .group = 10, .params = pageCV2 },
	{ .name = "CV Inputs",  .numParams = ARRAY_SIZE(pageCV3),       .group = 10, .params = pageCV3 },
	{ .name = "CV Inputs",  .numParams = ARRAY_SIZE(pageCV4),       .group = 10, .params = pageCV4 },
	{ .name = "Routing",    .numParams = ARRAY_SIZE(pageRouting),   .group = 11, .params = pageRouting },
};

static const _NT_parameterPages parameterPages = {
	.numPages = ARRAY_SIZE(pages),
	.pages = pages,
};

// ============================================================
// Algorithm struct (in SRAM)
//
// Main plugin instance. Lives in SRAM with pointers to DTC and DRAM.
// Contains a mutable copy of parameter definitions (for dynamic max
// values on folder/file params) and cached parameter values converted
// to floats for use in the audio thread.
// ============================================================

struct _pulsarAlgorithm : public _NT_algorithm
{
	_pulsarAlgorithm() {}
	~_pulsarAlgorithm() {}

	_NT_parameter params[kNumParams]; // Mutable copy of parameter definitions

	_pulsarDTC* dtc;                  // Pointer to DTC (fast per-sample state)
	_pulsarDRAM* dram;                // Pointer to DRAM (lookup tables + sample buffer)

	// Cached parameter values (converted from int16 to float in parameterChanged)
	float pulsaretIndex;              // 0.0–9.0: pulsaret morph position
	float windowIndex;                // 0.0–4.0: window morph position
	float dutyCycle;                  // 0.01–1.0: pulse duty cycle
	int dutyMode;                     // 0=manual, 1=formant-derived
	int formantCount;                 // 1–3: active formant count
	float formantHz[3];               // Formant frequencies in Hz
	int maskMode;                     // 0=off, 1=stochastic, 2=burst
	float maskAmount;                 // 0.0–1.0: stochastic mask probability
	int burstOn;                      // Burst pattern: consecutive sounding pulses
	int burstOff;                     // Burst pattern: consecutive muted pulses
	float attackMs;                   // Envelope attack time in ms
	float releaseMs;                  // Envelope release time in ms
	float amplitude;                  // 0.0–1.0: master amplitude
	float glideMs;                    // Glide/portamento time in ms
	float pan[3];                     // -1.0 to +1.0: per-formant stereo pan position
	int useSample;                    // 0=table pulsaret, 1=sample pulsaret
	float sampleRateRatio;            // 0.25–4.0: sample playback rate multiplier
	int gateMode;                     // 0=MIDI, 1=Free Run
	float basePitchHz;                // Hz from Base Pitch param
	float peakLevel;                  // Peak |output| over last block (for display)
	int voiceCount;                   // 1–4: active voice count
	int chordType;                  // 0–13: chord/interval type for Free Run

	// Display state (written by step at block rate, read by draw)
	// Volatile: step() and draw() may run in different interrupt contexts
	volatile float displayPulsaretIdx;         // Effective pulsaret index after CV
	volatile float displayWindowIdx;           // Effective window index after CV
	volatile float displayDuty;                // Effective duty cycle after CV (manual mode)
	volatile float displayFormantHz[3];        // Effective formant Hz after per-formant CV
	volatile float displayAmplitude;           // Effective amplitude after CV
	volatile float displayMask;                // Effective mask amount after CV
	volatile int displayActiveVoices;          // Number of voices currently sounding

	// Async SD card sample loading state
	_NT_wavRequest wavRequest;        // Persistent request struct for NT_readSampleFrames()
	bool cardMounted;                 // Tracks SD card mount state for change detection
	bool awaitingCallback;            // True while an async WAV load is in progress
	int sampleLoadedFrames;           // Number of valid frames in sampleBuffer
};

// ============================================================
// Helper: compute one-pole filter coefficient from time constant
//
// Returns the coefficient 'c' for a one-pole smoother:
//   y[n] = target + c * (y[n-1] - target)
// where ms is the desired time constant and sr is the sample rate.
// A time constant of 0 returns 0 (instant response).
// ============================================================

static float coeffFromMs(float ms, float sr)
{
	if (ms <= 0.0f) return 0.0f;
	float samples = ms * sr * 0.001f;
	if (samples < 1.0f) return 0.0f;
	return expf(-1.0f / samples);
}

// ============================================================
// Table generation
//
// Called once in construct() to fill DRAM lookup tables.
// All tables are 2048 samples, normalized to ±1.0 (pulsarets)
// or 0.0–1.0 (windows). Phase runs 0.0–1.0 across the table.
// ============================================================

// Pulsaret waveforms (10 tables):
//   0: sine         — pure sine wave
//   1: sine×2       — 2nd harmonic sine
//   2: sine×3       — 3rd harmonic sine
//   3: sinc         — sinc(8*pi*(p-0.5)), band-limited impulse
//   4: triangle     — triangle wave
//   5: saw          — sawtooth (rising ramp)
//   6: square       — 50% duty square wave
//   7: formant      — sine×3 with exponential decay envelope
//   8: pulse        — narrow Gaussian spike at center
//   9: noise        — deterministic pseudo-random noise (LCG)
static void generatePulsaretTables(float tables[][kTableSize])
{
	for (int i = 0; i < kTableSize; ++i)
	{
		float p = (float)i / (float)kTableSize;
		float twoPiP = 2.0f * (float)M_PI * p;

		// 0: sine
		tables[0][i] = sinf(twoPiP);

		// 1: sine x2 (2nd harmonic)
		tables[1][i] = sinf(2.0f * twoPiP);

		// 2: sine x3 (3rd harmonic)
		tables[2][i] = sinf(3.0f * twoPiP);

		// 3: sinc
		{
			float x = (p - 0.5f) * 8.0f * (float)M_PI;
			tables[3][i] = (fabsf(x) < 0.0001f) ? 1.0f : sinf(x) / x;
		}

		// 4: triangle
		{
			float t = 4.0f * p;
			if (p < 0.25f) tables[4][i] = t;
			else if (p < 0.75f) tables[4][i] = 2.0f - t;
			else tables[4][i] = t - 4.0f;
		}

		// 5: saw
		tables[5][i] = 2.0f * p - 1.0f;

		// 6: square
		tables[6][i] = (p < 0.5f) ? 1.0f : -1.0f;

		// 7: formant (sine with exponential decay)
		tables[7][i] = sinf(twoPiP * 3.0f) * expf(-3.0f * p);

		// 8: pulse (narrow spike)
		{
			float x = (p - 0.5f) * 20.0f;
			tables[8][i] = expf(-x * x);
		}

		// 9: noise — LCG seeded deterministically
		// We'll fill this separately
	}

	// 9: noise table (deterministic)
	uint32_t seed = 12345;
	for (int i = 0; i < kTableSize; ++i)
	{
		seed = seed * 1664525u + 1013904223u;
		tables[9][i] = (float)(int32_t)seed / 2147483648.0f;
	}
}

// Window functions (5 tables):
//   0: rectangular  — flat 1.0 (no windowing)
//   1: gaussian     — exp(-0.5 * ((p-0.5)/0.3)^2), sigma=0.3
//   2: hann         — 0.5 * (1 - cos(2*pi*p)), classic smooth window
//   3: exp decay    — exp(-4*p), sharp attack with gradual fade
//   4: linear decay — 1-p, simple ramp down
static void generateWindowTables(float tables[][kTableSize])
{
	for (int i = 0; i < kTableSize; ++i)
	{
		float p = (float)i / (float)(kTableSize - 1);

		// 0: rectangular
		tables[0][i] = 1.0f;

		// 1: gaussian (sigma=0.3)
		{
			float x = (p - 0.5f) / 0.3f;
			tables[1][i] = expf(-0.5f * x * x);
		}

		// 2: hann
		tables[2][i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * p));

		// 3: exponential decay
		tables[3][i] = expf(-4.0f * p);

		// 4: linear decay
		tables[4][i] = 1.0f - p;
	}
}

// ============================================================
// WAV callback — called asynchronously when sample loading completes
// ============================================================

static void wavCallback(void* callbackData, bool success)
{
	_pulsarAlgorithm* pThis = static_cast<_pulsarAlgorithm*>(callbackData);
	pThis->awaitingCallback = false;
	if (success)
		pThis->sampleLoadedFrames = pThis->wavRequest.numFrames;
}

// ============================================================
// calculateRequirements — tell the host how much memory we need
// ============================================================

static_assert( kNumParams == ARRAY_SIZE(parametersDefault) );

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t* specifications)
{
	req.numParameters = ARRAY_SIZE(parametersDefault);
	req.sram = sizeof(_pulsarAlgorithm);
	req.dram = sizeof(_pulsarDRAM);
	req.dtc = sizeof(_pulsarDTC);
	req.itc = 0;
}

// ============================================================
// construct — initialize a new plugin instance
//
// Called once when the algorithm is loaded into a slot.
// Sets up memory pointers, generates all lookup tables,
// initializes DTC state for all 4 voice slots, and configures
// the WAV request struct for async sample loading.
// ============================================================

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t* specifications)
{
	_pulsarAlgorithm* alg = new (ptrs.sram) _pulsarAlgorithm();

	alg->dtc = reinterpret_cast<_pulsarDTC*>(ptrs.dtc);
	alg->dram = reinterpret_cast<_pulsarDRAM*>(ptrs.dram);

	// Copy mutable parameters
	memcpy(alg->params, parametersDefault, sizeof(parametersDefault));
	alg->parameters = alg->params;
	alg->parameterPages = &parameterPages;

	// Initialize DTC (memset zeros all fields including all 4 voices)
	_pulsarDTC* dtc = alg->dtc;
	memset(dtc, 0, sizeof(_pulsarDTC));

	float sr = static_cast<float>(NT_globals.sampleRate);
	float dcCoeff = 1.0f - (2.0f * static_cast<float>(M_PI) * 25.0f / sr);
	float maskCoeff = coeffFromMs(3.0f, sr);

	for (int v = 0; v < kMaxVoices; ++v)
	{
		_pulsarVoice& voice = dtc->voices[v];
		voice.attackCoeff = 0.99f;
		voice.releaseCoeff = 0.999f;
		voice.prngState = 48271u + v * 12345u;
		voice.leakDC_coeff = dcCoeff;
		voice.maskSmoothCoeff = maskCoeff;
		for (int i = 0; i < 3; ++i)
		{
			voice.formantDuty[i] = 0.5f;
			voice.maskSmooth[i] = 1.0f;
			voice.maskTarget[i] = 1.0f;
		}
	}

	// Initialize algorithm cached values (placement new does NOT zero members)
	alg->pulsaretIndex = 2.5f;
	alg->windowIndex = 0.5f;
	alg->dutyCycle = 0.5f;
	alg->dutyMode = 0;
	alg->formantCount = 2;
	alg->formantHz[0] = 20.0f;
	alg->formantHz[1] = 200.0f;
	alg->formantHz[2] = 400.0f;
	alg->maskMode = 0;
	alg->maskAmount = 0.5f;
	alg->burstOn = 4;
	alg->burstOff = 4;
	alg->attackMs = 10.0f;
	alg->releaseMs = 200.0f;
	alg->amplitude = 0.0f;
	alg->glideMs = 0.0f;
	alg->pan[0] = 0.0f;
	alg->pan[1] = -0.5f;
	alg->pan[2] = 0.5f;
	alg->useSample = 0;
	alg->sampleRateRatio = 1.0f;
	alg->gateMode = 1;
	alg->basePitchHz = 440.0f * exp2f((24 - 69) / 12.0f);
	alg->peakLevel = 0.0f;
	alg->voiceCount = 1;
	alg->chordType = 0;
	alg->displayPulsaretIdx = 2.5f;
	alg->displayWindowIdx = 0.5f;
	alg->displayDuty = 0.5f;
	alg->displayFormantHz[0] = 20.0f;
	alg->displayFormantHz[1] = 200.0f;
	alg->displayFormantHz[2] = 400.0f;
	alg->displayAmplitude = 0.0f;
	alg->displayMask = 0.5f;
	alg->displayActiveVoices = 0;
	alg->cardMounted = false;
	alg->awaitingCallback = false;
	alg->sampleLoadedFrames = 0;

	// Setup WAV request
	alg->wavRequest.callback = wavCallback;
	alg->wavRequest.callbackData = alg;
	alg->wavRequest.bits = kNT_WavBits32;
	alg->wavRequest.channels = kNT_WavMono;
	alg->wavRequest.progress = kNT_WavProgress;
	alg->wavRequest.numFrames = kSampleBufferSize;
	alg->wavRequest.startOffset = 0;
	alg->wavRequest.dst = alg->dram->sampleBuffer;

	// Generate lookup tables, chord ratios, and clear sample buffer
	initChordRatios();
	generatePulsaretTables(alg->dram->pulsaretTables);
	generateWindowTables(alg->dram->windowTables);
	memset(alg->dram->sampleBuffer, 0, sizeof(alg->dram->sampleBuffer));

	return alg;
}

// ============================================================
// parameterString — display names for sample folder/file selectors
//
// Called by the host for parameters with kNT_unitHasStrings or
// kNT_unitConfirm. Returns the folder/file name from the SD card
// for display in the parameter UI instead of a raw numeric index.
// ============================================================

int parameterString(_NT_algorithm* self, int p, int v, char* buff)
{
	_pulsarAlgorithm* pThis = static_cast<_pulsarAlgorithm*>(self);
	int len = 0;

	switch (p)
	{
	case kParamFolder:
	{
		_NT_wavFolderInfo folderInfo;
		NT_getSampleFolderInfo(v, folderInfo);
		if (folderInfo.name)
		{
			strncpy(buff, folderInfo.name, kNT_parameterStringSize - 1);
			buff[kNT_parameterStringSize - 1] = 0;
			len = strlen(buff);
		}
	}
		break;
	case kParamFile:
	{
		_NT_wavInfo info;
		NT_getSampleFileInfo(pThis->v[kParamFolder], v, info);
		if (info.name)
		{
			strncpy(buff, info.name, kNT_parameterStringSize - 1);
			buff[kNT_parameterStringSize - 1] = 0;
			len = strlen(buff);
		}
	}
		break;
	}

	return len;
}

// ============================================================
// Helper: update Free Run voice frequencies from base pitch + intervals
// ============================================================

static void updateFreeRunVoices(_pulsarAlgorithm* pThis)
{
	_pulsarDTC* dtc = pThis->dtc;
	int vc = pThis->voiceCount;
	int intSet = pThis->chordType;

	for (int v = 0; v < kMaxVoices; ++v)
	{
		_pulsarVoice& voice = dtc->voices[v];
		if (v < vc)
		{
			float hz = pThis->basePitchHz * chordRatios[intSet][v];
			voice.gate = true;
			voice.envTarget = 1.0f;
			voice.velocity = 127;
			voice.targetFundamentalHz = hz;
			if (voice.fundamentalHz <= 0.0f || pThis->glideMs <= 0.0f)
				voice.fundamentalHz = hz;
		}
		else
		{
			voice.gate = false;
			voice.envTarget = 0.0f;
		}
	}
}

// ============================================================
// parameterChanged — convert raw int16 parameter values to floats
//
// Called by the host whenever a parameter value changes (from UI,
// MIDI, or CV). Converts integer parameter values to the float
// representations used by the audio thread, computes derived
// coefficients (envelope, glide), and manages parameter graying.
// ============================================================

void parameterChanged(_NT_algorithm* self, int p)
{
	_pulsarAlgorithm* pThis = static_cast<_pulsarAlgorithm*>(self);
	_pulsarDTC* dtc = pThis->dtc;
	float sr = static_cast<float>(NT_globals.sampleRate);
	int algIdx = NT_algorithmIndex(self);
	uint32_t offset = NT_parameterOffset();

	switch (p)
	{
	case kParamPulsaret:
		pThis->pulsaretIndex = pThis->v[kParamPulsaret] / 10.0f;
		break;
	case kParamWindow:
		pThis->windowIndex = pThis->v[kParamWindow] / 10.0f;
		break;
	case kParamDutyCycle:
		pThis->dutyCycle = pThis->v[kParamDutyCycle] / 100.0f;
		break;
	case kParamDutyMode:
		pThis->dutyMode = pThis->v[kParamDutyMode];
		break;

	case kParamFormantCount:
		pThis->formantCount = pThis->v[kParamFormantCount];
		// Gray out unused formant/pan params
		if (algIdx >= 0)
		{
			NT_setParameterGrayedOut(algIdx, kParamFormant2Hz + offset, pThis->formantCount < 2);
			NT_setParameterGrayedOut(algIdx, kParamFormant3Hz + offset, pThis->formantCount < 3);
			NT_setParameterGrayedOut(algIdx, kParamPan2 + offset, pThis->formantCount < 2);
			NT_setParameterGrayedOut(algIdx, kParamPan3 + offset, pThis->formantCount < 3);
		}
		break;
	case kParamFormant1Hz:
		pThis->formantHz[0] = (float)pThis->v[kParamFormant1Hz];
		break;
	case kParamFormant2Hz:
		pThis->formantHz[1] = (float)pThis->v[kParamFormant2Hz];
		break;
	case kParamFormant3Hz:
		pThis->formantHz[2] = (float)pThis->v[kParamFormant3Hz];
		break;

	case kParamMaskMode:
		pThis->maskMode = pThis->v[kParamMaskMode];
		// Gray out burst params when not in burst mode, mask amount when off
		if (algIdx >= 0)
		{
			NT_setParameterGrayedOut(algIdx, kParamMaskAmount + offset, pThis->maskMode == 0);
			NT_setParameterGrayedOut(algIdx, kParamBurstOn + offset, pThis->maskMode != 2);
			NT_setParameterGrayedOut(algIdx, kParamBurstOff + offset, pThis->maskMode != 2);
		}
		break;
	case kParamMaskAmount:
		pThis->maskAmount = pThis->v[kParamMaskAmount] / 100.0f;
		break;
	case kParamBurstOn:
		pThis->burstOn = pThis->v[kParamBurstOn];
		break;
	case kParamBurstOff:
		pThis->burstOff = pThis->v[kParamBurstOff];
		break;

	case kParamAttack:
		pThis->attackMs = pThis->v[kParamAttack] / 10.0f;
		for (int v = 0; v < kMaxVoices; ++v)
			dtc->voices[v].attackCoeff = coeffFromMs(pThis->attackMs, sr);
		break;
	case kParamRelease:
		pThis->releaseMs = pThis->v[kParamRelease] / 10.0f;
		for (int v = 0; v < kMaxVoices; ++v)
			dtc->voices[v].releaseCoeff = coeffFromMs(pThis->releaseMs, sr);
		break;
	case kParamAmplitude:
		pThis->amplitude = pThis->v[kParamAmplitude] / 100.0f;
		break;
	case kParamGlide:
		pThis->glideMs = pThis->v[kParamGlide] / 10.0f;
		for (int v = 0; v < kMaxVoices; ++v)
			dtc->voices[v].glideCoeff = coeffFromMs(pThis->glideMs, sr);
		break;

	case kParamPan1:
		pThis->pan[0] = pThis->v[kParamPan1] / 100.0f;
		break;
	case kParamPan2:
		pThis->pan[1] = pThis->v[kParamPan2] / 100.0f;
		break;
	case kParamPan3:
		pThis->pan[2] = pThis->v[kParamPan3] / 100.0f;
		break;

	case kParamUseSample:
		pThis->useSample = pThis->v[kParamUseSample];
		if (algIdx >= 0)
		{
			NT_setParameterGrayedOut(algIdx, kParamFolder + offset, !pThis->useSample);
			NT_setParameterGrayedOut(algIdx, kParamFile + offset, !pThis->useSample);
			NT_setParameterGrayedOut(algIdx, kParamSampleRate + offset, !pThis->useSample);
		}
		break;
	case kParamFolder:
	{
		_NT_wavFolderInfo folderInfo;
		NT_getSampleFolderInfo(pThis->v[kParamFolder], folderInfo);
		pThis->params[kParamFile].max = folderInfo.numSampleFiles - 1;
		if (algIdx >= 0)
			NT_updateParameterDefinition(algIdx, kParamFile);
	}
		break;
	case kParamFile:
		if (!pThis->awaitingCallback)
		{
			_NT_wavInfo info;
			NT_getSampleFileInfo(pThis->v[kParamFolder], pThis->v[kParamFile], info);
			int numFrames = info.numFrames;
			if (numFrames > kSampleBufferSize)
				numFrames = kSampleBufferSize;

			pThis->sampleLoadedFrames = 0;
			pThis->wavRequest.folder = pThis->v[kParamFolder];
			pThis->wavRequest.sample = pThis->v[kParamFile];
			pThis->wavRequest.numFrames = numFrames;
			if (NT_readSampleFrames(pThis->wavRequest))
				pThis->awaitingCallback = true;
		}
		break;
	case kParamSampleRate:
		pThis->sampleRateRatio = pThis->v[kParamSampleRate] / 100.0f;
		break;

	case kParamGateMode:
		pThis->gateMode = pThis->v[kParamGateMode];
		if (algIdx >= 0)
		{
			NT_setParameterGrayedOut(algIdx, kParamBasePitch + offset, pThis->gateMode == 0);
			NT_setParameterGrayedOut(algIdx, kParamMidiCh + offset, pThis->gateMode == 1);
			NT_setParameterGrayedOut(algIdx, kParamChordType + offset, pThis->gateMode == 0);
		}
		if (pThis->gateMode == 1)
		{
			// Free Run: set up all active voices with interval ratios
			updateFreeRunVoices(pThis);
		}
		else
		{
			// MIDI: release all voices gracefully
			for (int v = 0; v < kMaxVoices; ++v)
			{
				dtc->voices[v].gate = false;
				dtc->voices[v].envTarget = 0.0f;
			}
		}
		break;
	case kParamBasePitch:
		pThis->basePitchHz = 440.0f * exp2f((pThis->v[kParamBasePitch] - 69) / 12.0f);
		if (pThis->gateMode == 1)
			updateFreeRunVoices(pThis);
		break;

	case kParamVoiceCount:
		pThis->voiceCount = pThis->v[kParamVoiceCount];
		if (pThis->gateMode == 1)
			updateFreeRunVoices(pThis);
		break;
	case kParamChordType:
		pThis->chordType = pThis->v[kParamChordType];
		if (pThis->gateMode == 1)
			updateFreeRunVoices(pThis);
		break;
	}
}

// ============================================================
// MIDI handling — polyphonic voice allocation
//
// Responds to note on/off on the configured MIDI channel.
// Voice allocation priority:
//   1. Retrigger: voice already playing this note
//   2. Free voice: released voice with lowest envelope
//   3. Steal: oldest voice (LRU via voiceAge)
//
// Note off: find voice with matching note + gate, release it.
// Velocity 0 note-on is treated as note-off per MIDI convention.
// ============================================================

void midiMessage(_NT_algorithm* self, uint8_t byte0, uint8_t byte1, uint8_t byte2)
{
	_pulsarAlgorithm* pThis = static_cast<_pulsarAlgorithm*>(self);
	_pulsarDTC* dtc = pThis->dtc;

	// Free Run mode ignores MIDI notes
	if (pThis->v[kParamGateMode] == 1)
		return;

	int channel = byte0 & 0x0f;
	int status = byte0 & 0xf0;

	if (channel != (pThis->v[kParamMidiCh] - 1))
		return;

	int voiceCount = pThis->voiceCount;

	switch (status)
	{
	case 0x80: // note off
	{
		for (int v = 0; v < voiceCount; ++v)
		{
			if (dtc->voices[v].currentNote == byte1 && dtc->voices[v].gate)
			{
				dtc->voices[v].gate = false;
				dtc->voices[v].envTarget = 0.0f;
				break;
			}
		}
	}
		break;
	case 0x90: // note on
		if (byte2 == 0)
		{
			// velocity 0 = note off
			for (int v = 0; v < voiceCount; ++v)
			{
				if (dtc->voices[v].currentNote == byte1 && dtc->voices[v].gate)
				{
					dtc->voices[v].gate = false;
					dtc->voices[v].envTarget = 0.0f;
					break;
				}
			}
		}
		else
		{
			// Find voice to assign
			int chosen = -1;

			// 1. Retrigger: voice already playing this note
			for (int v = 0; v < voiceCount; ++v)
			{
				if (dtc->voices[v].currentNote == byte1 && dtc->voices[v].gate)
				{
					chosen = v;
					break;
				}
			}

			// 2. Free voice: released voice with lowest envelope
			if (chosen < 0)
			{
				float lowestEnv = 2.0f;
				for (int v = 0; v < voiceCount; ++v)
				{
					if (!dtc->voices[v].gate && dtc->voices[v].envValue < lowestEnv)
					{
						lowestEnv = dtc->voices[v].envValue;
						chosen = v;
					}
				}
			}

			// 3. Steal: oldest voice (lowest voiceAge)
			if (chosen < 0)
			{
				uint8_t oldestAge = dtc->voiceAge[0];
				chosen = 0;
				for (int v = 1; v < voiceCount; ++v)
				{
					int8_t diff = (int8_t)(dtc->voiceAge[v] - oldestAge);
					if (diff < 0)
					{
						oldestAge = dtc->voiceAge[v];
						chosen = v;
					}
				}
			}

			// Assign note to chosen voice
			_pulsarVoice& voice = dtc->voices[chosen];
			voice.currentNote = byte1;
			voice.velocity = byte2;
			voice.gate = true;
			voice.envTarget = 1.0f;
			voice.targetFundamentalHz = 440.0f * exp2f((byte1 - 69) / 12.0f);
			if (pThis->glideMs <= 0.0f || voice.fundamentalHz <= 0.0f)
				voice.fundamentalHz = voice.targetFundamentalHz;
			dtc->voiceAge[chosen] = dtc->nextVoiceAge++;
		}
		break;
	}
}

// ============================================================
// Inline helpers for audio processing
//
// These are called per-sample in the inner loop and must be fast.
// All table reads use linear interpolation with power-of-2 wrapping.
// ============================================================

// Read a single table with linear interpolation.
// phase is 0.0–1.0, tableSize must be a power of 2 for the bitmask wrap.
static inline float readTableLerp(const float* table, int tableSize, float phase)
{
	float pos = phase * tableSize;
	int idx = (int)pos;
	float frac = pos - idx;
	idx &= (tableSize - 1);
	int idx2 = (idx + 1) & (tableSize - 1);
	return table[idx] + frac * (table[idx2] - table[idx]);
}

// Read from the pulsaret table bank with bilinear morphing.
// index is 0.0–9.0: integer part selects two adjacent tables,
// fractional part crossfades between them.
static inline float readTableMorph(const float tables[][kTableSize], float index, float phase)
{
	int idx0 = (int)index;
	float frac = index - idx0;
	if (idx0 < 0) { idx0 = 0; frac = 0.0f; }
	if (idx0 >= kNumPulsarets - 1) { idx0 = kNumPulsarets - 2; frac = 1.0f; }
	float s0 = readTableLerp(tables[idx0], kTableSize, phase);
	float s1 = readTableLerp(tables[idx0 + 1], kTableSize, phase);
	return s0 + frac * (s1 - s0);
}

// Read from the window table bank with bilinear morphing.
// Same as readTableMorph but clamped to kNumWindows.
static inline float readWindowMorph(const float tables[][kTableSize], float index, float phase)
{
	int idx0 = (int)index;
	float frac = index - idx0;
	if (idx0 < 0) { idx0 = 0; frac = 0.0f; }
	if (idx0 >= kNumWindows - 1) { idx0 = kNumWindows - 2; frac = 1.0f; }
	float s0 = readTableLerp(tables[idx0], kTableSize, phase);
	float s1 = readTableLerp(tables[idx0 + 1], kTableSize, phase);
	return s0 + frac * (s1 - s0);
}

// Fast Padé approximation of tanh for soft clipping.
// tanh(x) ≈ x(27+x²)/(27+9x²), accurate to <1% for |x| < 3.
static inline float fastTanh(float x)
{
	float x2 = x * x;
	return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Fast exp2 approximation for 1V/oct pitch CV processing.
// Uses integer bit manipulation + cubic polynomial refinement.
// Accurate to ~1 cent over [-4, 4] range (±4 octaves).
static inline float fastExp2f(float x)
{
	float fi = floorf(x);
	float f = x - fi;
	// Cubic polynomial for 2^f on [0,1)
	float p = f * (f * (f * 0.079441f + 0.227411f) + 0.693147f) + 1.0f;
	// Apply integer part by adding to IEEE 754 exponent bits
	union { float fv; int32_t iv; } u;
	u.fv = p;
	u.iv += (int32_t)fi << 23;
	return u.fv;
}

// ============================================================
// step — main audio processing
//
// Called by the host at the audio sample rate in blocks of
// numFramesBy4*4 frames. busFrames points to all 64 bus buffers
// laid out contiguously (numFrames per bus). This function:
//
//   1. Reads CV input busses and computes per-block averages
//   2. Precomputes per-formant pan gains outside the sample loop
//   3. Per sample: for each voice, advances master phase, detects
//      pulse triggers, evaluates mask, synthesizes pulsaret×window
//      for each formant, pans to stereo, applies envelope and
//      velocity, DC-blocks. Sums voices, normalizes, soft-clips,
//      and writes to output busses.
//
// Compiled with -O2 (via attribute) for better loop optimization
// while the rest of the plugin uses -Os.
// ============================================================

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4)
{
	_pulsarAlgorithm* pThis = static_cast<_pulsarAlgorithm*>(self);
	_pulsarDTC* dtc = pThis->dtc;
	_pulsarDRAM* dram = pThis->dram;

	int numFrames = numFramesBy4 * 4;
	if (numFrames < 1) return;
	float sr = static_cast<float>(NT_globals.sampleRate);

	int voiceCount = pThis->voiceCount;
	int chordType = pThis->chordType;

	// Free Run: ensure voice state is correct every block
	bool freeRunMode = (pThis->v[kParamGateMode] == 1);
	if (freeRunMode)
	{
		for (int v = 0; v < voiceCount; ++v)
		{
			_pulsarVoice& voice = dtc->voices[v];
			voice.gate = true;
			// envTarget managed per-sample for per-pulse AR envelope
			voice.velocity = 127;
			if (voice.targetFundamentalHz <= 0.0f)
			{
				float hz = pThis->basePitchHz * chordRatios[chordType][v];
				voice.targetFundamentalHz = hz;
				voice.fundamentalHz = hz;
			}
		}
	}

	// Output bus pointers
	float* outL = busFrames + (pThis->v[kParamOutputL] - 1) * numFrames;
	float* outR = busFrames + (pThis->v[kParamOutputR] - 1) * numFrames;
	bool replaceL = pThis->v[kParamOutputLMode];
	bool replaceR = pThis->v[kParamOutputRMode];

	// CV input bus pointers
	float* cvPitch = NULL;
	float* cvDuty = NULL;
	float* cvMask = NULL;
	float* cvPulsaret = NULL;
	float* cvWindow = NULL;
	float* cvAmplitude = NULL;
	float* cvFormant1 = NULL;
	float* cvFormant2 = NULL;
	float* cvFormant3 = NULL;
	float* cvPan1 = NULL;
	float* cvAttack = NULL;
	float* cvRelease = NULL;
	if (pThis->v[kParamPitchCV] > 0)
		cvPitch = busFrames + (pThis->v[kParamPitchCV] - 1) * numFrames;
	if (pThis->v[kParamDutyCV] > 0)
		cvDuty = busFrames + (pThis->v[kParamDutyCV] - 1) * numFrames;
	if (pThis->v[kParamMaskCV] > 0)
		cvMask = busFrames + (pThis->v[kParamMaskCV] - 1) * numFrames;
	if (pThis->v[kParamPulsaretCV] > 0)
		cvPulsaret = busFrames + (pThis->v[kParamPulsaretCV] - 1) * numFrames;
	if (pThis->v[kParamWindowCV] > 0)
		cvWindow = busFrames + (pThis->v[kParamWindowCV] - 1) * numFrames;
	if (pThis->v[kParamAmplitudeCV] > 0)
		cvAmplitude = busFrames + (pThis->v[kParamAmplitudeCV] - 1) * numFrames;
	if (pThis->v[kParamFormant1CV] > 0)
		cvFormant1 = busFrames + (pThis->v[kParamFormant1CV] - 1) * numFrames;
	if (pThis->v[kParamFormant2CV] > 0)
		cvFormant2 = busFrames + (pThis->v[kParamFormant2CV] - 1) * numFrames;
	if (pThis->v[kParamFormant3CV] > 0)
		cvFormant3 = busFrames + (pThis->v[kParamFormant3CV] - 1) * numFrames;
	if (pThis->v[kParamPan1CV] > 0)
		cvPan1 = busFrames + (pThis->v[kParamPan1CV] - 1) * numFrames;
	if (pThis->v[kParamAttackCV] > 0)
		cvAttack = busFrames + (pThis->v[kParamAttackCV] - 1) * numFrames;
	if (pThis->v[kParamReleaseCV] > 0)
		cvRelease = busFrames + (pThis->v[kParamReleaseCV] - 1) * numFrames;

	// SD card mount detection
	bool cardMounted = NT_isSdCardMounted();
	if (pThis->cardMounted != cardMounted)
	{
		pThis->cardMounted = cardMounted;
		if (cardMounted)
		{
			int algIdx = NT_algorithmIndex(self);
			pThis->params[kParamFolder].max = NT_getNumSampleFolders() - 1;
			if (algIdx >= 0)
				NT_updateParameterDefinition(algIdx, kParamFolder);
		}
	}

	// Read cached parameters
	float pulsaretIdx = pThis->pulsaretIndex;
	float windowIdx = pThis->windowIndex;
	float baseDuty = pThis->dutyCycle;
	int dutyMode = pThis->dutyMode;
	int formantCount = pThis->formantCount;
	float amplitude = pThis->amplitude;
	int maskMode = pThis->maskMode;
	float maskAmount = pThis->maskAmount;
	int burstOn = pThis->burstOn;
	int burstOff = pThis->burstOff;
	int useSample = pThis->useSample;
	float sampleRateRatio = pThis->sampleRateRatio;

	// Read per-block CV averages (single combined loop)
	float cvDutyAvg = 0.0f;
	float cvMaskAvg = 0.0f;
	float cvPulsaretAvg = 0.0f;
	float cvWindowAvg = 0.0f;
	float cvAmplitudeAvg = 0.0f;
	float cvFormant1Avg = 0.0f;
	float cvFormant2Avg = 0.0f;
	float cvFormant3Avg = 0.0f;
	float cvPan1Avg = 0.0f;
	float cvAttackAvg = 0.0f;
	float cvReleaseAvg = 0.0f;
	{
		for (int i = 0; i < numFrames; ++i)
		{
			if (cvDuty) cvDutyAvg += cvDuty[i];
			if (cvMask) cvMaskAvg += cvMask[i];
			if (cvPulsaret) cvPulsaretAvg += cvPulsaret[i];
			if (cvWindow) cvWindowAvg += cvWindow[i];
			if (cvAmplitude) cvAmplitudeAvg += cvAmplitude[i];
			if (cvFormant1) cvFormant1Avg += cvFormant1[i];
			if (cvFormant2) cvFormant2Avg += cvFormant2[i];
			if (cvFormant3) cvFormant3Avg += cvFormant3[i];
			if (cvPan1) cvPan1Avg += cvPan1[i];
			if (cvAttack) cvAttackAvg += cvAttack[i];
			if (cvRelease) cvReleaseAvg += cvRelease[i];
		}
		float invNumFrames = 1.0f / (float)numFrames;
		if (cvDuty) cvDutyAvg *= invNumFrames;
		if (cvMask) cvMaskAvg *= invNumFrames;
		if (cvPulsaret) cvPulsaretAvg *= invNumFrames;
		if (cvWindow) cvWindowAvg *= invNumFrames;
		if (cvAmplitude) cvAmplitudeAvg *= invNumFrames;
		if (cvFormant1) cvFormant1Avg *= invNumFrames;
		if (cvFormant2) cvFormant2Avg *= invNumFrames;
		if (cvFormant3) cvFormant3Avg *= invNumFrames;
		if (cvPan1) cvPan1Avg *= invNumFrames;
		if (cvAttack) cvAttackAvg *= invNumFrames;
		if (cvRelease) cvReleaseAvg *= invNumFrames;
	}

	// Duty CV: bipolar ±5V → ±20% offset
	float dutyCvOffset = cvDutyAvg * 0.04f;

	// Mask CV: bipolar ±5V → ±50% offset on mask amount
	float effectiveMask = maskAmount + cvMaskAvg * 0.1f;
	if (effectiveMask < 0.0f) effectiveMask = 0.0f;
	if (effectiveMask > 1.0f) effectiveMask = 1.0f;

	// Pulsaret CV: bipolar ±5V → ±4.5 offset on index (full range sweep)
	pulsaretIdx += cvPulsaretAvg * 0.9f;
	if (pulsaretIdx < 0.0f) pulsaretIdx = 0.0f;
	if (pulsaretIdx > 9.0f) pulsaretIdx = 9.0f;

	// Window CV: bipolar ±5V → ±2.0 offset on index (full range sweep)
	windowIdx += cvWindowAvg * 0.4f;
	if (windowIdx < 0.0f) windowIdx = 0.0f;
	if (windowIdx > 4.0f) windowIdx = 4.0f;

	// Amplitude CV: bipolar ±5V → ±50% offset on amplitude
	float effectiveAmplitude = amplitude + cvAmplitudeAvg * 0.1f;
	if (effectiveAmplitude < 0.0f) effectiveAmplitude = 0.0f;
	if (effectiveAmplitude > 1.0f) effectiveAmplitude = 1.0f;

	// Formant 1-3 CV: bipolar ±5V → ±1000 Hz offset, clamped to [20, 2000]
	float modulatedFormantHz[3];
	modulatedFormantHz[0] = pThis->formantHz[0] + cvFormant1Avg * 200.0f;
	if (modulatedFormantHz[0] < 20.0f) modulatedFormantHz[0] = 20.0f;
	if (modulatedFormantHz[0] > 2000.0f) modulatedFormantHz[0] = 2000.0f;
	modulatedFormantHz[1] = pThis->formantHz[1] + cvFormant2Avg * 200.0f;
	if (modulatedFormantHz[1] < 20.0f) modulatedFormantHz[1] = 20.0f;
	if (modulatedFormantHz[1] > 2000.0f) modulatedFormantHz[1] = 2000.0f;
	modulatedFormantHz[2] = pThis->formantHz[2] + cvFormant3Avg * 200.0f;
	if (modulatedFormantHz[2] < 20.0f) modulatedFormantHz[2] = 20.0f;
	if (modulatedFormantHz[2] > 2000.0f) modulatedFormantHz[2] = 2000.0f;

	// Attack CV: bipolar ±5V → ±1000 ms offset on attack time
	float modulatedAttackCoeff = dtc->voices[0].attackCoeff;
	if (cvAttack)
	{
		float modAttackMs = pThis->attackMs + cvAttackAvg * 200.0f;
		if (modAttackMs < 0.1f) modAttackMs = 0.1f;
		if (modAttackMs > 2000.0f) modAttackMs = 2000.0f;
		modulatedAttackCoeff = coeffFromMs(modAttackMs, sr);
	}

	// Release CV: bipolar ±5V → ±1600 ms offset on release time
	float modulatedReleaseCoeff = dtc->voices[0].releaseCoeff;
	if (cvRelease)
	{
		float modReleaseMs = pThis->releaseMs + cvReleaseAvg * 320.0f;
		if (modReleaseMs < 1.0f) modReleaseMs = 1.0f;
		if (modReleaseMs > 3200.0f) modReleaseMs = 3200.0f;
		modulatedReleaseCoeff = coeffFromMs(modReleaseMs, sr);
	}

	// Update display state for draw() — reflects CV modulation in realtime
	pThis->displayPulsaretIdx = pulsaretIdx;
	pThis->displayWindowIdx = windowIdx;
	float effDuty = baseDuty + dutyCvOffset;
	if (effDuty < 0.01f) effDuty = 0.01f;
	if (effDuty > 1.0f) effDuty = 1.0f;
	pThis->displayDuty = effDuty;
	pThis->displayFormantHz[0] = modulatedFormantHz[0];
	pThis->displayFormantHz[1] = modulatedFormantHz[1];
	pThis->displayFormantHz[2] = modulatedFormantHz[2];
	pThis->displayAmplitude = effectiveAmplitude;
	pThis->displayMask = effectiveMask;

	// Precompute per-formant pan gains (shared across all voices)
	float panL[3], panR[3];
	for (int f = 0; f < formantCount; ++f)
	{
		float p = pThis->pan[f];
		// Pan 1 CV: bipolar ±5V → ±1.0 offset on pan position
		if (f == 0 && cvPan1)
		{
			p += cvPan1Avg * 0.2f;
			if (p < -1.0f) p = -1.0f;
			if (p > 1.0f) p = 1.0f;
		}
		float angle = (p + 1.0f) * 0.25f * (float)M_PI; // 0..pi/2
		panL[f] = cosf(angle);
		panR[f] = sinf(angle);
	}

	// Precompute manual duty per formant (shared across voices in manual mode)
	float manualDuty[3];
	for (int f = 0; f < formantCount; ++f)
	{
		manualDuty[f] = baseDuty + dutyCvOffset;
		if (manualDuty[f] < 0.01f) manualDuty[f] = 0.01f;
		if (manualDuty[f] > 1.0f) manualDuty[f] = 1.0f;
	}

	float invFormantCount = 1.0f / (float)formantCount;
	float invSr = 1.0f / sr;
	float invVoiceCount = 1.0f / (float)voiceCount;

	float peak = 0.0f;
	int activeVoices = 0;

	// Sample loop
	for (int i = 0; i < numFrames; ++i)
	{
		float totalL = 0.0f;
		float totalR = 0.0f;

		for (int vi = 0; vi < voiceCount; ++vi)
		{
			_pulsarVoice& voice = dtc->voices[vi];

			// Early exit: skip silent released voices
			if (!voice.gate && voice.envValue < 0.0001f)
				continue;

			// Track active voices (only on first sample for display)
			if (i == 0) ++activeVoices;

			// Glide: one-pole lag on frequency
			float glideC = voice.glideCoeff;
			voice.fundamentalHz = voice.targetFundamentalHz + glideC * (voice.fundamentalHz - voice.targetFundamentalHz);

			// Per-sample pitch CV (1V/oct) — shifts all voices proportionally
			float freqHz = voice.fundamentalHz;
			if (cvPitch)
				freqHz *= fastExp2f(cvPitch[i]);

			// Advance master phase
			float phaseInc = freqHz * invSr;
			if (phaseInc < 0.0f) phaseInc = 0.0f;
			if (phaseInc > 0.5f) phaseInc = 0.5f;

			voice.masterPhase += phaseInc;

			// Detect new pulse trigger (phase wrap)
			bool newPulse = false;
			if (voice.masterPhase >= 1.0f)
			{
				voice.masterPhase -= 1.0f;
				newPulse = true;
			}

			// Masking: update target on new pulse (per-voice PRNG/burst state)
			if (maskMode > 0 && newPulse)
			{
				float maskGain = 1.0f;
				if (maskMode == 1)
				{
					// Stochastic: LCG PRNG vs threshold
					voice.prngState = voice.prngState * 1664525u + 1013904223u;
					float rnd = (float)(voice.prngState >> 8) / 16777216.0f;
					maskGain = (rnd < effectiveMask) ? 0.0f : 1.0f;
				}
				else if (maskMode == 2)
				{
					// Burst: on for burstOn, off for burstOff
					int total = burstOn + burstOff;
					if (total > 0)
					{
						voice.burstCounter = (voice.burstCounter + 1) % (uint32_t)total;
						maskGain = (voice.burstCounter < (uint32_t)burstOn) ? 1.0f : 0.0f;
					}
				}
				for (int f = 0; f < formantCount; ++f)
					voice.maskTarget[f] = maskGain;
			}

			// Smooth mask continuously every sample toward target
			float maskCoeff = voice.maskSmoothCoeff;
			for (int f = 0; f < formantCount; ++f)
				voice.maskSmooth[f] = voice.maskTarget[f] + maskCoeff * (voice.maskSmooth[f] - voice.maskTarget[f]);

			// Synthesis: accumulate formants
			float sumL = 0.0f;
			float sumR = 0.0f;
			float phase = voice.masterPhase;

			for (int f = 0; f < formantCount; ++f)
			{
				// Compute per-voice formant duty
				float duty;
				if (dutyMode == 1 && freqHz > 0.0f)
				{
					duty = freqHz / modulatedFormantHz[f];
					if (duty > 1.0f) duty = 1.0f;
				}
				else
				{
					duty = manualDuty[f];
				}

				if (phase < duty)
				{
					float pulsaretPhase = phase / duty;
					float sample;

					if (useSample && pThis->sampleLoadedFrames >= 2)
					{
						// Sample-based pulsaret
						float samplePos = pulsaretPhase * (pThis->sampleLoadedFrames - 1) * sampleRateRatio;
						int sIdx = (int)samplePos;
						float sFrac = samplePos - sIdx;
						if (sIdx < 0) sIdx = 0;
						if (sIdx >= pThis->sampleLoadedFrames - 1) sIdx = pThis->sampleLoadedFrames - 2;
						sample = dram->sampleBuffer[sIdx] + sFrac * (dram->sampleBuffer[sIdx + 1] - dram->sampleBuffer[sIdx]);
					}
					else
					{
						// Table-based pulsaret with morphing
						float formantRatio = modulatedFormantHz[f] / (freqHz > 0.1f ? freqHz : 0.1f);
						float tablePhase = pulsaretPhase * formantRatio;
						tablePhase -= static_cast<float>(static_cast<int>(tablePhase));
						sample = readTableMorph(dram->pulsaretTables, pulsaretIdx, tablePhase);
					}

					// Window with morphing
					float window = readWindowMorph(dram->windowTables, windowIdx, pulsaretPhase);

					float s = sample * window * voice.maskSmooth[f];

					// Pan to stereo (constant power)
					sumL += s * panL[f];
					sumR += s * panR[f];
				}
			}

			// Normalize by formant count
			sumL *= invFormantCount;
			sumR *= invFormantCount;

			// Envelope
			if (freeRunMode)
			{
				// Per-pulse AR: attack on new pulse, release at period midpoint
				if (newPulse) voice.envTarget = 1.0f;
				if (voice.masterPhase >= 0.5f && voice.envTarget > 0.5f) voice.envTarget = 0.0f;
				float envCoeff = (voice.envTarget > 0.5f) ? modulatedAttackCoeff : modulatedReleaseCoeff;
				voice.envValue = voice.envTarget + envCoeff * (voice.envValue - voice.envTarget);
			}
			else
			{
				// MIDI: ASR envelope (one-pole smoother)
				float envCoeff = voice.gate ? modulatedAttackCoeff : modulatedReleaseCoeff;
				voice.envValue = voice.envTarget + envCoeff * (voice.envValue - voice.envTarget);
			}

			float vel = voice.velocity * (1.0f / 127.0f);
			float gain = voice.envValue * effectiveAmplitude * vel;
			sumL *= gain;
			sumR *= gain;

			// DC-blocking highpass per voice (independent filter state)
			float dcCoeff = voice.leakDC_coeff;
			float xL = sumL;
			float yL = xL - voice.leakDC_xL + dcCoeff * voice.leakDC_yL;
			voice.leakDC_xL = xL;
			voice.leakDC_yL = yL;

			float xR = sumR;
			float yR = xR - voice.leakDC_xR + dcCoeff * voice.leakDC_yR;
			voice.leakDC_xR = xR;
			voice.leakDC_yR = yR;

			// Accumulate into voice sum
			totalL += yL;
			totalR += yR;
		}

		// Normalize by voice count (param value, not active count — avoids volume jumps)
		totalL *= invVoiceCount;
		totalR *= invVoiceCount;

		// Soft clip once on summed output (fast Pade tanh)
		totalL = fastTanh(totalL);
		totalR = fastTanh(totalR);

		// Write to output
		if (replaceL)
			outL[i] = totalL;
		else
			outL[i] += totalL;

		if (replaceR)
			outR[i] = totalR;
		else
			outR[i] += totalR;

		// Track peak output level for display
		float absL = totalL < 0.0f ? -totalL : totalL;
		float absR = totalR < 0.0f ? -totalR : totalR;
		float m = absL > absR ? absL : absR;
		if (m > peak) peak = m;
	}
	pThis->peakLevel = peak;
	pThis->displayActiveVoices = activeVoices;
}

// ============================================================
// draw — custom display rendering
//
// Called by the host to render the algorithm's display (256×64 px).
// Draws: pulsaret×window waveform preview, fundamental frequency
// readout, envelope level bar, gate indicator, formant count,
// voice count, and interval set label.
// Returns false to keep the standard parameter line at the top.
// ============================================================

bool draw(_NT_algorithm* self)
{
	_pulsarAlgorithm* pThis = static_cast<_pulsarAlgorithm*>(self);
	_pulsarDTC* dtc = pThis->dtc;
	_pulsarDRAM* dram = pThis->dram;

	// Waveform visualization: draw pulsaret * window shape
	int waveX = 10;
	int waveY = 30;
	int waveW = 150;
	int waveH = 24;

	float pulsaretIdx = pThis->displayPulsaretIdx;
	float windowIdx = pThis->displayWindowIdx;
	float duty = pThis->displayDuty;

	// Draw bounding box
	NT_drawShapeI(kNT_box, waveX - 1, waveY - waveH / 2 - 1, waveX + waveW + 1, waveY + waveH / 2 + 1, 3);

	int prevY = waveY;
	for (int x = 0; x < waveW; ++x)
	{
		float p = (float)x / (float)waveW;
		float s = 0.0f;
		if (p < duty)
		{
			float pp = p / duty;
			float formantRatio = pThis->displayFormantHz[0] / (dtc->voices[0].fundamentalHz > 0.1f ? dtc->voices[0].fundamentalHz : 0.1f);
			float tp = pp * formantRatio;
			tp -= (int)tp;
			if (tp < 0.0f) tp += 1.0f;
			s = readTableMorph(dram->pulsaretTables, pulsaretIdx, tp);
			s *= readWindowMorph(dram->windowTables, windowIdx, pp);
		}
		int pixY = waveY - (int)(s * waveH / 2);
		if (x > 0)
			NT_drawShapeI(kNT_line, waveX + x - 1, prevY, waveX + x, pixY, 15);
		prevY = pixY;
	}

	// Frequency readout (voice 0)
	char buf[32];
	int len = NT_floatToString(buf, dtc->voices[0].fundamentalHz, 1);
	buf[len] = 0;
	NT_drawText(waveX + waveW + 8, waveY - 8, buf, 15, kNT_textLeft, kNT_textTiny);
	NT_drawText(waveX + waveW + 8, waveY, "Hz", 10, kNT_textLeft, kNT_textTiny);

	// Envelope level bar (max envelope across all voices)
	int barX = waveX + waveW + 8;
	int barY = waveY + 8;
	int barW = 30;
	int barH = 4;
	NT_drawShapeI(kNT_box, barX, barY, barX + barW, barY + barH, 5);
	float maxEnv = 0.0f;
	bool anyGate = false;
	for (int v = 0; v < pThis->voiceCount; ++v)
	{
		if (dtc->voices[v].envValue > maxEnv) maxEnv = dtc->voices[v].envValue;
		if (dtc->voices[v].gate) anyGate = true;
	}
	int fillW = (int)(maxEnv * barW);
	if (fillW > 0)
		NT_drawShapeI(kNT_rectangle, barX, barY, barX + fillW, barY + barH, 15);

	// Gate indicator (lit if any voice gated)
	if (anyGate)
		NT_drawShapeI(kNT_rectangle, barX + barW + 4, barY, barX + barW + 8, barY + barH, 15);

	// Formant count + voice count
	char fcBuf[8];
	fcBuf[0] = '0' + pThis->formantCount;
	fcBuf[1] = 'F';
	fcBuf[2] = ' ';
	fcBuf[3] = '0' + pThis->voiceCount;
	fcBuf[4] = 'V';
	fcBuf[5] = 0;
	NT_drawText(waveX + waveW + 8, waveY - 16, fcBuf, 8, kNT_textLeft, kNT_textTiny);

	// Gate mode indicator
	if (pThis->v[kParamGateMode] == 1)
		NT_drawText(barX + barW + 12, barY, "FR", 15, kNT_textLeft, kNT_textTiny);

	// Chord type label (Free Run mode, dimmed when only 1 voice)
	if (pThis->v[kParamGateMode] == 1)
	{
		static const char* chordLabels[] = {
			"UNI", "OCT", "5TH", "SUB",
			"MAJ", "MIN", "MA7", "MI7",
			"SU4", "DM7", "DIM", "AUG",
			"PWR", "OP5"
		};
		int ct = pThis->chordType;
		int bright = (pThis->voiceCount > 1) ? 10 : 4;
		if (ct >= 0 && ct < kNumChordTypes)
			NT_drawText(barX + barW + 12, barY + barH + 4, chordLabels[ct], bright, kNT_textLeft, kNT_textTiny);
	}

	// Peak output level bar (shows if synthesis is producing signal)
	int pkBarX = waveX;
	int pkBarY = waveY + waveH / 2 + 4;
	int pkBarW = waveW;
	int pkBarH = 3;
	NT_drawShapeI(kNT_box, pkBarX, pkBarY, pkBarX + pkBarW, pkBarY + pkBarH, 3);
	int pkFillW = (int)(pThis->peakLevel * pkBarW);
	if (pkFillW > pkBarW) pkFillW = pkBarW;
	if (pkFillW > 0)
		NT_drawShapeI(kNT_rectangle, pkBarX, pkBarY, pkBarX + pkFillW, pkBarY + pkBarH, 15);

	// Formant frequency readouts (show effective Hz after CV modulation)
	{
		int fmtY = pkBarY + pkBarH + 4;
		int formantCount = pThis->formantCount;
		char fbuf[16];
		for (int f = 0; f < 3; ++f)
		{
			int brightness = (f < formantCount) ? 15 : 4;
			int xPos = waveX + f * 56;

			// Label: "F1" "F2" "F3"
			char label[4];
			label[0] = 'F';
			label[1] = '1' + f;
			label[2] = ':';
			label[3] = 0;
			NT_drawText(xPos, fmtY, label, brightness, kNT_textLeft, kNT_textTiny);

			// Value
			int fLen = NT_floatToString(fbuf, pThis->displayFormantHz[f], 0);
			fbuf[fLen] = 0;
			NT_drawText(xPos + 16, fmtY, fbuf, brightness, kNT_textLeft, kNT_textTiny);
		}

		// Amplitude readout (right side, below envelope bar)
		int aLen = NT_floatToString(fbuf, pThis->displayAmplitude * 100.0f, 0);
		fbuf[aLen] = '%';
		fbuf[aLen + 1] = 0;
		NT_drawText(barX, barY + barH + 4, fbuf, 10, kNT_textLeft, kNT_textTiny);
	}

	return false;
}

// ============================================================
// Custom UI — hardware pot and encoder button mappings
//
// Overrides 3 pots and 2 encoder buttons for direct hands-on
// control. All other controls (encoders, buttons 1–4) retain
// standard disting NT page navigation behavior.
//
//   Pot L:             Pulsaret morph (0.0–9.0)
//   Pot C:             Duty Cycle (1–100%)
//   Pot R:             Window morph (0.0–4.0)
//   Encoder Button L:  Cycle mask mode (Off → Stochastic → Burst)
//   Encoder Button R:  Cycle formant count (1 → 2 → 3)
//   Button 3:          Cycle voice count (1 → 2 → 3 → 4)
//   Button 4:          Cycle chord type (14 options)
//
// setupUi() syncs pot soft-takeover positions so pots don't
// jump when first touched after switching to this algorithm.
// ============================================================

uint32_t hasCustomUi(_NT_algorithm* self)
{
	return kNT_potL | kNT_potC | kNT_potR | kNT_encoderButtonL | kNT_encoderButtonR | kNT_button3 | kNT_button4;
}

void customUi(_NT_algorithm* self, const _NT_uiData& data)
{
	int algIdx = NT_algorithmIndex(self);
	if (algIdx < 0) return;
	uint32_t offset = NT_parameterOffset();

	// Pot L: Pulsaret morph (0.0–9.0, stored as 0–90 with scaling10)
	if (data.controls & kNT_potL)
	{
		int value = (int)(data.pots[0] * 90.0f + 0.5f);
		NT_setParameterFromUi(algIdx, kParamPulsaret + offset, (int16_t)value);
	}

	// Pot C: Duty Cycle (1–100%)
	if (data.controls & kNT_potC)
	{
		int value = (int)(data.pots[1] * 99.0f + 0.5f) + 1;
		NT_setParameterFromUi(algIdx, kParamDutyCycle + offset, (int16_t)value);
	}

	// Pot R: Window morph (0.0–4.0, stored as 0–40 with scaling10)
	if (data.controls & kNT_potR)
	{
		int value = (int)(data.pots[2] * 40.0f + 0.5f);
		NT_setParameterFromUi(algIdx, kParamWindow + offset, (int16_t)value);
	}

	// Encoder Button L: cycle mask mode (Off -> Stochastic -> Burst -> Off)
	if ((data.controls & kNT_encoderButtonL) && !(data.lastButtons & kNT_encoderButtonL))
	{
		int mode = (self->v[kParamMaskMode] + 1) % 3;
		NT_setParameterFromUi(algIdx, kParamMaskMode + offset, (int16_t)mode);
	}

	// Encoder Button R: cycle formant count (1 -> 2 -> 3 -> 1)
	if ((data.controls & kNT_encoderButtonR) && !(data.lastButtons & kNT_encoderButtonR))
	{
		int count = self->v[kParamFormantCount] % 3 + 1;
		NT_setParameterFromUi(algIdx, kParamFormantCount + offset, (int16_t)count);
	}

	// Button 3: cycle voice count (1 -> 2 -> 3 -> 4 -> 1)
	if ((data.controls & kNT_button3) && !(data.lastButtons & kNT_button3))
	{
		int vc = self->v[kParamVoiceCount] % 4 + 1;
		NT_setParameterFromUi(algIdx, kParamVoiceCount + offset, (int16_t)vc);
	}

	// Button 4: cycle chord type (0 -> 1 -> ... -> 13 -> 0)
	if ((data.controls & kNT_button4) && !(data.lastButtons & kNT_button4))
	{
		int ct = (self->v[kParamChordType] + 1) % kNumChordTypes;
		NT_setParameterFromUi(algIdx, kParamChordType + offset, (int16_t)ct);
	}
}

void setupUi(_NT_algorithm* self, _NT_float3& pots)
{
	// Sync pot soft-takeover positions
	pots[0] = self->v[kParamPulsaret] / 90.0f;  // Pulsaret
	pots[1] = (self->v[kParamDutyCycle] - 1) / 99.0f;  // Duty Cycle
	pots[2] = self->v[kParamWindow] / 40.0f;  // Window
}

// ============================================================
// Factory definition + plugin entry point
//
// The factory struct registers all callbacks with the disting NT
// host. pluginEntry() is the single exported symbol that the
// host calls to discover this plugin's factories.
// ============================================================

static const _NT_factory factory =
{
	.guid = NT_MULTICHAR('S', 'r', 'P', 's'),
	.name = "Spaluter",
	.description = "Pulsar synthesis with formants, masking, and CV",
	.numSpecifications = 0,
	.calculateRequirements = calculateRequirements,
	.construct = construct,
	.parameterChanged = parameterChanged,
	.step = step,
	.draw = draw,
	.midiMessage = midiMessage,
	.tags = kNT_tagInstrument,
	.hasCustomUi = hasCustomUi,
	.customUi = customUi,
	.setupUi = setupUi,
	.parameterString = parameterString,
};

uintptr_t pluginEntry(_NT_selector selector, uint32_t data)
{
	switch (selector)
	{
	case kNT_selector_version:
		return kNT_apiVersionCurrent;
	case kNT_selector_numFactories:
		return 1;
	case kNT_selector_factoryInfo:
		return (uintptr_t)((data == 0) ? &factory : NULL);
	}
	return 0;
}
