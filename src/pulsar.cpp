#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>
#include <new>
#include <distingnt/api.h>
#include <distingnt/wav.h>
#include <distingnt/serialisation.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// ============================================================
// Table sizes
// ============================================================
static const int kTableSize = 2048;
static const int kNumPulsarets = 10;
static const int kNumWindows = 5;
static const int kSampleBufferSize = 48000;

// ============================================================
// Memory structures
// ============================================================

struct _pulsarDRAM {
	float pulsaretTables[kNumPulsarets][kTableSize];
	float windowTables[kNumWindows][kTableSize];
	float sampleBuffer[kSampleBufferSize];
};

struct _pulsarDTC {
	float masterPhase;
	float fundamentalHz;
	float targetFundamentalHz;
	float glideCoeff;
	float formantDuty[3];
	float maskSmooth[3];
	float maskTarget[3];
	float maskSmoothCoeff;
	float envValue;
	float envTarget;
	float attackCoeff;
	float releaseCoeff;
	float leakDC_xL;
	float leakDC_yL;
	float leakDC_xR;
	float leakDC_yR;
	float leakDC_coeff;
	uint8_t currentNote;
	uint8_t velocity;
	bool gate;
	bool prevPulseActive;
	uint32_t prngState;
	uint32_t burstCounter;
	float cvPitchOffset;
	float cvFormantMod;
	float cvDutyMod;
	float cvMaskMod;
};

// ============================================================
// Parameter enums
// ============================================================

enum {
	// Synthesis page
	kParamPulsaret,
	kParamWindow,
	kParamDutyCycle,
	kParamDutyMode,

	// Formants page
	kParamFormantCount,
	kParamFormant1Hz,
	kParamFormant2Hz,
	kParamFormant3Hz,

	// Masking page
	kParamMaskMode,
	kParamMaskAmount,
	kParamBurstOn,
	kParamBurstOff,

	// Envelope page
	kParamAttack,
	kParamRelease,
	kParamAmplitude,
	kParamGlide,

	// Panning page
	kParamPan1,
	kParamPan2,
	kParamPan3,

	// Sample page
	kParamUseSample,
	kParamFolder,
	kParamFile,
	kParamSampleRate,

	// CV Inputs page
	kParamPitchCV,
	kParamFormantCV,
	kParamDutyCV,
	kParamMaskCV,

	// Routing page
	kParamMidiCh,
	kParamOutputL,
	kParamOutputLMode,
	kParamOutputR,
	kParamOutputRMode,

	kNumParams,
};

// ============================================================
// Enum strings
// ============================================================

static char const * const enumDutyMode[] = { "Manual", "Formant" };
static char const * const enumMaskMode[] = { "Off", "Stochastic", "Burst" };
static char const * const enumUseSample[] = { "Off", "On" };

// ============================================================
// Parameter definitions
// ============================================================

static const _NT_parameter parametersDefault[] = {
	// Synthesis page
	{ .name = "Pulsaret",    .min = 0,   .max = 90,    .def = 0,   .unit = kNT_unitNone,    .scaling = kNT_scaling10, .enumStrings = NULL },
	{ .name = "Window",      .min = 0,   .max = 40,    .def = 20,  .unit = kNT_unitNone,    .scaling = kNT_scaling10, .enumStrings = NULL },
	{ .name = "Duty Cycle",  .min = 1,   .max = 100,   .def = 50,  .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = NULL },
	{ .name = "Duty Mode",   .min = 0,   .max = 1,     .def = 0,   .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = enumDutyMode },

	// Formants page
	{ .name = "Formant Count", .min = 1,  .max = 3,    .def = 1,   .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = NULL },
	{ .name = "Formant 1 Hz",  .min = 20, .max = 8000, .def = 440, .unit = kNT_unitHz,      .scaling = kNT_scalingNone, .enumStrings = NULL },
	{ .name = "Formant 2 Hz",  .min = 20, .max = 8000, .def = 880, .unit = kNT_unitHz,      .scaling = kNT_scalingNone, .enumStrings = NULL },
	{ .name = "Formant 3 Hz",  .min = 20, .max = 8000, .def = 1320,.unit = kNT_unitHz,      .scaling = kNT_scalingNone, .enumStrings = NULL },

	// Masking page
	{ .name = "Mask Mode",   .min = 0,   .max = 2,     .def = 0,   .unit = kNT_unitEnum,    .scaling = kNT_scalingNone, .enumStrings = enumMaskMode },
	{ .name = "Mask Amount", .min = 0,   .max = 100,   .def = 50,  .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = NULL },
	{ .name = "Burst On",    .min = 1,   .max = 16,    .def = 4,   .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = NULL },
	{ .name = "Burst Off",   .min = 0,   .max = 16,    .def = 4,   .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = NULL },

	// Envelope page
	{ .name = "Attack",      .min = 1,   .max = 20000, .def = 100, .unit = kNT_unitMs,      .scaling = kNT_scaling10, .enumStrings = NULL },
	{ .name = "Release",     .min = 10,  .max = 32000, .def = 2000,.unit = kNT_unitMs,      .scaling = kNT_scaling10, .enumStrings = NULL },
	{ .name = "Amplitude",   .min = 0,   .max = 100,   .def = 80,  .unit = kNT_unitPercent, .scaling = kNT_scalingNone, .enumStrings = NULL },
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

	// CV Inputs page
	NT_PARAMETER_CV_INPUT( "Pitch CV",   0, 0 )
	NT_PARAMETER_CV_INPUT( "Formant CV", 0, 0 )
	NT_PARAMETER_CV_INPUT( "Duty CV",    0, 0 )
	NT_PARAMETER_CV_INPUT( "Mask CV",    0, 0 )

	// Routing page
	{ .name = "MIDI Ch",     .min = 1,   .max = 16,    .def = 1,   .unit = kNT_unitNone,    .scaling = kNT_scalingNone, .enumStrings = NULL },
	NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Output L", 1, 13 )
	NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE( "Output R", 1, 14 )
};

// ============================================================
// Parameter pages
// ============================================================

static const uint8_t pageSynthesis[] = { kParamPulsaret, kParamWindow, kParamDutyCycle, kParamDutyMode };
static const uint8_t pageFormants[]  = { kParamFormantCount, kParamFormant1Hz, kParamFormant2Hz, kParamFormant3Hz };
static const uint8_t pageMasking[]   = { kParamMaskMode, kParamMaskAmount, kParamBurstOn, kParamBurstOff };
static const uint8_t pageEnvelope[]  = { kParamAttack, kParamRelease, kParamAmplitude, kParamGlide };
static const uint8_t pagePanning[]   = { kParamPan1, kParamPan2, kParamPan3 };
static const uint8_t pageSample[]    = { kParamUseSample, kParamFolder, kParamFile, kParamSampleRate };
static const uint8_t pageCV[]        = { kParamPitchCV, kParamFormantCV, kParamDutyCV, kParamMaskCV };
static const uint8_t pageRouting[]   = { kParamMidiCh, kParamOutputL, kParamOutputLMode, kParamOutputR, kParamOutputRMode };

static const _NT_parameterPage pages[] = {
	{ .name = "Synthesis", .numParams = ARRAY_SIZE(pageSynthesis), .group = 0, .unused = {0,0}, .params = pageSynthesis },
	{ .name = "Formants",  .numParams = ARRAY_SIZE(pageFormants),  .group = 0, .unused = {0,0}, .params = pageFormants },
	{ .name = "Masking",   .numParams = ARRAY_SIZE(pageMasking),   .group = 0, .unused = {0,0}, .params = pageMasking },
	{ .name = "Envelope",  .numParams = ARRAY_SIZE(pageEnvelope),  .group = 0, .unused = {0,0}, .params = pageEnvelope },
	{ .name = "Panning",   .numParams = ARRAY_SIZE(pagePanning),   .group = 0, .unused = {0,0}, .params = pagePanning },
	{ .name = "Sample",    .numParams = ARRAY_SIZE(pageSample),    .group = 0, .unused = {0,0}, .params = pageSample },
	{ .name = "CV Inputs", .numParams = ARRAY_SIZE(pageCV),        .group = 0, .unused = {0,0}, .params = pageCV },
	{ .name = "Routing",   .numParams = ARRAY_SIZE(pageRouting),   .group = 0, .unused = {0,0}, .params = pageRouting },
};

static const _NT_parameterPages parameterPages = {
	.numPages = ARRAY_SIZE(pages),
	.pages = pages,
};

// ============================================================
// Algorithm struct (in SRAM)
// ============================================================

struct _pulsarAlgorithm : public _NT_algorithm
{
	_pulsarAlgorithm() {}
	~_pulsarAlgorithm() {}

	_NT_parameter params[kNumParams];

	_pulsarDTC* dtc;
	_pulsarDRAM* dram;

	// Cached parameter values
	float pulsaretIndex;
	float windowIndex;
	float dutyCycle;
	int dutyMode;
	int formantCount;
	float formantHz[3];
	int maskMode;
	float maskAmount;
	int burstOn;
	int burstOff;
	float attackMs;
	float releaseMs;
	float amplitude;
	float glideMs;
	float pan[3];
	int useSample;
	float sampleRateRatio;

	// Sample loading state
	_NT_wavRequest wavRequest;
	bool cardMounted;
	bool awaitingCallback;
	int sampleLoadedFrames;
};

// ============================================================
// Helper: compute one-pole coefficient from time in ms
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
// ============================================================

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
// WAV callback
// ============================================================

static void wavCallback(void* callbackData, bool success)
{
	_pulsarAlgorithm* pThis = (_pulsarAlgorithm*)callbackData;
	pThis->awaitingCallback = false;
}

// ============================================================
// calculateRequirements
// ============================================================

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t* specifications)
{
	req.numParameters = kNumParams;
	req.sram = sizeof(_pulsarAlgorithm);
	req.dram = sizeof(_pulsarDRAM);
	req.dtc = sizeof(_pulsarDTC);
	req.itc = 0;
}

// ============================================================
// construct
// ============================================================

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t* specifications)
{
	_pulsarAlgorithm* alg = new (ptrs.sram) _pulsarAlgorithm();

	alg->dtc = (_pulsarDTC*)ptrs.dtc;
	alg->dram = (_pulsarDRAM*)ptrs.dram;

	// Copy mutable parameters
	memcpy(alg->params, parametersDefault, sizeof(parametersDefault));
	alg->parameters = alg->params;
	alg->parameterPages = &parameterPages;

	// Initialize DTC
	_pulsarDTC* dtc = alg->dtc;
	memset(dtc, 0, sizeof(_pulsarDTC));
	dtc->masterPhase = 0.0f;
	dtc->fundamentalHz = 0.0f;
	dtc->targetFundamentalHz = 0.0f;
	dtc->glideCoeff = 0.0f;
	dtc->envValue = 0.0f;
	dtc->envTarget = 0.0f;
	dtc->attackCoeff = 0.99f;
	dtc->releaseCoeff = 0.999f;
	dtc->gate = false;
	dtc->prevPulseActive = false;
	dtc->prngState = 48271u;
	dtc->burstCounter = 0;
	dtc->currentNote = 0;
	dtc->velocity = 0;
	// LeakDC coefficient: target ~25 Hz cutoff, sample-rate independent
	float sr = (float)NT_globals.sampleRate;
	dtc->leakDC_coeff = 1.0f - (2.0f * (float)M_PI * 25.0f / sr);
	// Mask smoothing coefficient: ~3 ms time constant
	dtc->maskSmoothCoeff = coeffFromMs(3.0f, sr);
	for (int i = 0; i < 3; ++i)
	{
		dtc->formantDuty[i] = 0.5f;
		dtc->maskSmooth[i] = 1.0f;
		dtc->maskTarget[i] = 1.0f;
	}

	// Initialize algorithm cached values
	alg->pulsaretIndex = 0.0f;
	alg->windowIndex = 2.0f; // hann default
	alg->dutyCycle = 0.5f;
	alg->dutyMode = 0;
	alg->formantCount = 1;
	alg->formantHz[0] = 440.0f;
	alg->formantHz[1] = 880.0f;
	alg->formantHz[2] = 1320.0f;
	alg->maskMode = 0;
	alg->maskAmount = 0.5f;
	alg->burstOn = 4;
	alg->burstOff = 4;
	alg->attackMs = 10.0f;
	alg->releaseMs = 200.0f;
	alg->amplitude = 0.8f;
	alg->glideMs = 0.0f;
	alg->pan[0] = 0.0f;
	alg->pan[1] = -0.5f;
	alg->pan[2] = 0.5f;
	alg->useSample = 0;
	alg->sampleRateRatio = 1.0f;
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

	// Generate lookup tables
	generatePulsaretTables(alg->dram->pulsaretTables);
	generateWindowTables(alg->dram->windowTables);

	// Clear sample buffer
	memset(alg->dram->sampleBuffer, 0, sizeof(alg->dram->sampleBuffer));

	return alg;
}

// ============================================================
// parameterString — for sample folder/file names
// ============================================================

int parameterString(_NT_algorithm* self, int p, int v, char* buff)
{
	_pulsarAlgorithm* pThis = (_pulsarAlgorithm*)self;
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
// parameterChanged
// ============================================================

void parameterChanged(_NT_algorithm* self, int p)
{
	_pulsarAlgorithm* pThis = (_pulsarAlgorithm*)self;
	_pulsarDTC* dtc = pThis->dtc;
	float sr = (float)NT_globals.sampleRate;
	int algIdx = NT_algorithmIndex(self);

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
			NT_setParameterGrayedOut(algIdx, kParamFormant2Hz, pThis->formantCount < 2);
			NT_setParameterGrayedOut(algIdx, kParamFormant3Hz, pThis->formantCount < 3);
			NT_setParameterGrayedOut(algIdx, kParamPan2, pThis->formantCount < 2);
			NT_setParameterGrayedOut(algIdx, kParamPan3, pThis->formantCount < 3);
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
			NT_setParameterGrayedOut(algIdx, kParamMaskAmount, pThis->maskMode == 0);
			NT_setParameterGrayedOut(algIdx, kParamBurstOn, pThis->maskMode != 2);
			NT_setParameterGrayedOut(algIdx, kParamBurstOff, pThis->maskMode != 2);
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
		dtc->attackCoeff = coeffFromMs(pThis->attackMs, sr);
		break;
	case kParamRelease:
		pThis->releaseMs = pThis->v[kParamRelease] / 10.0f;
		dtc->releaseCoeff = coeffFromMs(pThis->releaseMs, sr);
		break;
	case kParamAmplitude:
		pThis->amplitude = pThis->v[kParamAmplitude] / 100.0f;
		break;
	case kParamGlide:
		pThis->glideMs = pThis->v[kParamGlide] / 10.0f;
		dtc->glideCoeff = coeffFromMs(pThis->glideMs, sr);
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
			NT_setParameterGrayedOut(algIdx, kParamFolder, !pThis->useSample);
			NT_setParameterGrayedOut(algIdx, kParamFile, !pThis->useSample);
			NT_setParameterGrayedOut(algIdx, kParamSampleRate, !pThis->useSample);
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
		if (!pThis->awaitingCallback && pThis->useSample)
		{
			_NT_wavInfo info;
			NT_getSampleFileInfo(pThis->v[kParamFolder], pThis->v[kParamFile], info);
			pThis->sampleLoadedFrames = info.numFrames;
			if ((int)pThis->sampleLoadedFrames > kSampleBufferSize)
				pThis->sampleLoadedFrames = kSampleBufferSize;

			pThis->wavRequest.folder = pThis->v[kParamFolder];
			pThis->wavRequest.sample = pThis->v[kParamFile];
			pThis->wavRequest.numFrames = pThis->sampleLoadedFrames;
			if (NT_readSampleFrames(pThis->wavRequest))
				pThis->awaitingCallback = true;
		}
		break;
	case kParamSampleRate:
		pThis->sampleRateRatio = pThis->v[kParamSampleRate] / 100.0f;
		break;
	}
}

// ============================================================
// MIDI handling
// ============================================================

void midiMessage(_NT_algorithm* self, uint8_t byte0, uint8_t byte1, uint8_t byte2)
{
	_pulsarAlgorithm* pThis = (_pulsarAlgorithm*)self;
	_pulsarDTC* dtc = pThis->dtc;

	int channel = byte0 & 0x0f;
	int status = byte0 & 0xf0;

	if (channel != (pThis->v[kParamMidiCh] - 1))
		return;

	switch (status)
	{
	case 0x80: // note off
		if (byte1 == dtc->currentNote)
		{
			dtc->gate = false;
			dtc->envTarget = 0.0f;
		}
		break;
	case 0x90: // note on
		if (byte2 == 0)
		{
			// velocity 0 = note off
			if (byte1 == dtc->currentNote)
			{
				dtc->gate = false;
				dtc->envTarget = 0.0f;
			}
		}
		else
		{
			dtc->currentNote = byte1;
			dtc->velocity = byte2;
			dtc->gate = true;
			dtc->envTarget = 1.0f;
			dtc->targetFundamentalHz = 440.0f * exp2f((byte1 - 69) / 12.0f);
			// If no glide or first note, snap frequency
			if (pThis->glideMs <= 0.0f || dtc->fundamentalHz <= 0.0f)
				dtc->fundamentalHz = dtc->targetFundamentalHz;
		}
		break;
	}
}

// ============================================================
// Inline helpers for audio
// ============================================================

static inline float readTableLerp(const float* table, int tableSize, float phase)
{
	float pos = phase * tableSize;
	int idx = (int)pos;
	float frac = pos - idx;
	idx &= (tableSize - 1);
	int idx2 = (idx + 1) & (tableSize - 1);
	return table[idx] + frac * (table[idx2] - table[idx]);
}

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

static inline float fastTanh(float x)
{
	float x2 = x * x;
	return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static inline float fastExp2f(float x)
{
	// Fast exp2 approximation via integer bit manipulation + cubic refinement
	// Accurate to ~1 cent over [-4, 4] range (sufficient for 1V/oct CV)
	float fi = floorf(x);
	float f = x - fi;
	// Cubic polynomial for 2^f on [0,1): max error ~0.01 cents
	float p = f * (f * (f * 0.079441f + 0.227411f) + 0.693147f) + 1.0f;
	// Apply integer part via bit manipulation
	union { float fv; int32_t iv; } u;
	u.fv = p;
	u.iv += (int32_t)fi << 23;
	return u.fv;
}

// ============================================================
// step — main audio processing
// ============================================================

void __attribute__((optimize("O2"))) step(_NT_algorithm* self, float* busFrames, int numFramesBy4)
{
	_pulsarAlgorithm* pThis = (_pulsarAlgorithm*)self;
	_pulsarDTC* dtc = pThis->dtc;
	_pulsarDRAM* dram = pThis->dram;

	int numFrames = numFramesBy4 * 4;
	float sr = (float)NT_globals.sampleRate;

	// Output bus pointers
	float* outL = busFrames + (pThis->v[kParamOutputL] - 1) * numFrames;
	float* outR = busFrames + (pThis->v[kParamOutputR] - 1) * numFrames;
	bool replaceL = pThis->v[kParamOutputLMode];
	bool replaceR = pThis->v[kParamOutputRMode];

	// CV input bus pointers
	float* cvPitch = NULL;
	float* cvFormant = NULL;
	float* cvDuty = NULL;
	float* cvMask = NULL;
	if (pThis->v[kParamPitchCV] > 0)
		cvPitch = busFrames + (pThis->v[kParamPitchCV] - 1) * numFrames;
	if (pThis->v[kParamFormantCV] > 0)
		cvFormant = busFrames + (pThis->v[kParamFormantCV] - 1) * numFrames;
	if (pThis->v[kParamDutyCV] > 0)
		cvDuty = busFrames + (pThis->v[kParamDutyCV] - 1) * numFrames;
	if (pThis->v[kParamMaskCV] > 0)
		cvMask = busFrames + (pThis->v[kParamMaskCV] - 1) * numFrames;

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

	// Read per-block CV averages for formant/duty/mask (single combined loop)
	float cvFormantAvg = 0.0f;
	float cvDutyAvg = 0.0f;
	float cvMaskAvg = 0.0f;
	if (cvFormant || cvDuty || cvMask)
	{
		for (int i = 0; i < numFrames; ++i)
		{
			if (cvFormant) cvFormantAvg += cvFormant[i];
			if (cvDuty) cvDutyAvg += cvDuty[i];
			if (cvMask) cvMaskAvg += cvMask[i];
		}
		float invNumFrames = 1.0f / (float)numFrames;
		if (cvFormant) cvFormantAvg *= invNumFrames;
		if (cvDuty) cvDutyAvg *= invNumFrames;
		if (cvMask) cvMaskAvg *= invNumFrames;
	}

	// Formant CV: bipolar +-5V -> +-50% multiplier
	float formantCvMul = 1.0f + cvFormantAvg * 0.1f;
	// Duty CV: bipolar +-5V -> +-20% offset
	float dutyCvOffset = cvDutyAvg * 0.04f;
	// Mask CV: unipolar 0-10V -> 0-1
	float maskCvAmount = cvMaskAvg * 0.1f;
	if (maskCvAmount < 0.0f) maskCvAmount = 0.0f;
	if (maskCvAmount > 1.0f) maskCvAmount = 1.0f;

	// Precompute per-formant pan gains
	float panL[3], panR[3];
	for (int f = 0; f < formantCount; ++f)
	{
		float p = pThis->pan[f];
		float angle = (p + 1.0f) * 0.25f * (float)M_PI; // 0..pi/2
		panL[f] = cosf(angle);
		panR[f] = sinf(angle);
	}

	// Per-formant duty
	float formantDuty[3];
	for (int f = 0; f < formantCount; ++f)
	{
		if (dutyMode == 1 && dtc->fundamentalHz > 0.0f)
		{
			// Formant-derived duty: duty = fundamental / formant
			float fHz = pThis->formantHz[f] * formantCvMul;
			if (fHz < 20.0f) fHz = 20.0f;
			formantDuty[f] = dtc->fundamentalHz / fHz;
			if (formantDuty[f] > 1.0f) formantDuty[f] = 1.0f;
		}
		else
		{
			formantDuty[f] = baseDuty + dutyCvOffset;
		}
		if (formantDuty[f] < 0.01f) formantDuty[f] = 0.01f;
		if (formantDuty[f] > 1.0f) formantDuty[f] = 1.0f;
	}

	float invFormantCount = 1.0f / (float)formantCount;
	float invSr = 1.0f / sr;

	// Precompute reciprocal of duty per formant
	float invDuty[3];
	for (int f = 0; f < formantCount; ++f)
		invDuty[f] = 1.0f / formantDuty[f];

	// Precompute formant ratio when pitch CV is not connected (constant across block)
	float formantRatioPrecomp[3];
	bool hasPitchCV = (cvPitch != NULL);
	if (!hasPitchCV)
	{
		float invFund = 1.0f / (dtc->fundamentalHz > 0.1f ? dtc->fundamentalHz : 0.1f);
		for (int f = 0; f < formantCount; ++f)
			formantRatioPrecomp[f] = pThis->formantHz[f] * formantCvMul * invFund;
	}

	// Mask smooth coefficient (sample-rate dependent, from DTC)
	float maskSmoothCoeff = dtc->maskSmoothCoeff;

	// Sample loop
	for (int i = 0; i < numFrames; ++i)
	{
		// Glide: one-pole lag on frequency
		float glideC = dtc->glideCoeff;
		dtc->fundamentalHz = dtc->targetFundamentalHz + glideC * (dtc->fundamentalHz - dtc->targetFundamentalHz);

		// Per-sample pitch CV (1V/oct)
		float freqHz = dtc->fundamentalHz;
		if (cvPitch)
			freqHz *= fastExp2f(cvPitch[i]);

		// Advance master phase
		float phaseInc = freqHz * invSr;
		if (phaseInc < 0.0f) phaseInc = 0.0f;
		if (phaseInc > 0.5f) phaseInc = 0.5f;

		dtc->masterPhase += phaseInc;

		// Detect new pulse trigger (phase wrap)
		bool newPulse = false;
		if (dtc->masterPhase >= 1.0f)
		{
			dtc->masterPhase -= 1.0f;
			newPulse = true;
		}

		// Masking: update target on new pulse
		if (maskMode > 0 && newPulse)
		{
			float maskGain = 1.0f;
			if (maskMode == 1)
			{
				// Stochastic: LCG PRNG vs threshold
				float effectiveAmount = maskAmount;
				if (cvMask) effectiveAmount = maskCvAmount;
				dtc->prngState = dtc->prngState * 1664525u + 1013904223u;
				float rnd = (float)(dtc->prngState >> 8) / 16777216.0f;
				maskGain = (rnd < effectiveAmount) ? 0.0f : 1.0f;
			}
			else if (maskMode == 2)
			{
				// Burst: on for burstOn, off for burstOff
				int total = burstOn + burstOff;
				if (total > 0)
				{
					dtc->burstCounter = (dtc->burstCounter + 1) % (uint32_t)total;
					maskGain = (dtc->burstCounter < (uint32_t)burstOn) ? 1.0f : 0.0f;
				}
			}
			for (int f = 0; f < formantCount; ++f)
				dtc->maskTarget[f] = maskGain;
		}

		// Smooth mask continuously every sample toward target
		for (int f = 0; f < formantCount; ++f)
			dtc->maskSmooth[f] = dtc->maskTarget[f] + maskSmoothCoeff * (dtc->maskSmooth[f] - dtc->maskTarget[f]);

		// Synthesis: accumulate formants
		float sumL = 0.0f;
		float sumR = 0.0f;
		float phase = dtc->masterPhase;

		for (int f = 0; f < formantCount; ++f)
		{
			float duty = formantDuty[f];

			if (phase < duty)
			{
				float pulsaretPhase = phase * invDuty[f];
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
					float formantRatio;
					if (hasPitchCV)
						formantRatio = pThis->formantHz[f] * formantCvMul / (dtc->fundamentalHz > 0.1f ? dtc->fundamentalHz : 0.1f);
					else
						formantRatio = formantRatioPrecomp[f];
					float tablePhase = pulsaretPhase * formantRatio;
					tablePhase -= floorf(tablePhase);
					sample = readTableMorph(dram->pulsaretTables, pulsaretIdx, tablePhase);
				}

				// Window with morphing
				float window = readWindowMorph(dram->windowTables, windowIdx, pulsaretPhase);

				float s = sample * window * dtc->maskSmooth[f];

				// Pan to stereo (constant power)
				sumL += s * panL[f];
				sumR += s * panR[f];
			}
		}

		// Normalize by formant count
		sumL *= invFormantCount;
		sumR *= invFormantCount;

		// ASR envelope (one-pole smoother)
		float envCoeff = dtc->gate ? dtc->attackCoeff : dtc->releaseCoeff;
		dtc->envValue = dtc->envTarget + envCoeff * (dtc->envValue - dtc->envTarget);

		float vel = dtc->velocity * (1.0f / 127.0f);
		sumL *= dtc->envValue * amplitude * vel;
		sumR *= dtc->envValue * amplitude * vel;

		// LeakDC highpass: y = x - x_prev + coeff * y_prev (sample-rate dependent)
		float dcCoeff = dtc->leakDC_coeff;
		float xL = sumL;
		float yL = xL - dtc->leakDC_xL + dcCoeff * dtc->leakDC_yL;
		dtc->leakDC_xL = xL;
		dtc->leakDC_yL = yL;

		float xR = sumR;
		float yR = xR - dtc->leakDC_xR + dcCoeff * dtc->leakDC_yR;
		dtc->leakDC_xR = xR;
		dtc->leakDC_yR = yR;

		// Soft clip (fast Pade tanh)
		yL = fastTanh(yL);
		yR = fastTanh(yR);

		// Write to output
		if (replaceL)
			outL[i] = yL;
		else
			outL[i] += yL;

		if (replaceR)
			outR[i] = yR;
		else
			outR[i] += yR;
	}
}

// ============================================================
// draw
// ============================================================

bool draw(_NT_algorithm* self)
{
	_pulsarAlgorithm* pThis = (_pulsarAlgorithm*)self;
	_pulsarDTC* dtc = pThis->dtc;
	_pulsarDRAM* dram = pThis->dram;

	// Waveform visualization: draw pulsaret * window shape
	int waveX = 10;
	int waveY = 30;
	int waveW = 100;
	int waveH = 24;

	float pulsaretIdx = pThis->pulsaretIndex;
	float windowIdx = pThis->windowIndex;
	float duty = pThis->dutyCycle;
	if (duty < 0.01f) duty = 0.01f;

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
			float formantRatio = pThis->formantHz[0] / (dtc->fundamentalHz > 0.1f ? dtc->fundamentalHz : 100.0f);
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

	// Frequency readout
	char buf[32];
	int len = NT_floatToString(buf, dtc->fundamentalHz, 1);
	buf[len] = 0;
	NT_drawText(waveX + waveW + 8, waveY - 8, buf, 15, kNT_textLeft, kNT_textTiny);
	NT_drawText(waveX + waveW + 8, waveY, "Hz", 10, kNT_textLeft, kNT_textTiny);

	// Envelope level bar
	int barX = waveX + waveW + 8;
	int barY = waveY + 8;
	int barW = 30;
	int barH = 4;
	NT_drawShapeI(kNT_box, barX, barY, barX + barW, barY + barH, 5);
	int fillW = (int)(dtc->envValue * barW);
	if (fillW > 0)
		NT_drawShapeI(kNT_rectangle, barX, barY, barX + fillW, barY + barH, 15);

	// Gate indicator
	if (dtc->gate)
		NT_drawShapeI(kNT_rectangle, barX + barW + 4, barY, barX + barW + 8, barY + barH, 15);

	// Formant count
	char fcBuf[8];
	fcBuf[0] = '0' + pThis->formantCount;
	fcBuf[1] = 'F';
	fcBuf[2] = 0;
	NT_drawText(waveX + waveW + 8, waveY - 16, fcBuf, 8, kNT_textLeft, kNT_textTiny);

	return false;
}

// ============================================================
// Serialization — save/restore sample selection
// ============================================================

void serialise(_NT_algorithm* self, _NT_jsonStream& stream)
{
	_pulsarAlgorithm* pThis = (_pulsarAlgorithm*)self;

	stream.addMemberName("sampleFolder");
	stream.addNumber((int)pThis->v[kParamFolder]);

	stream.addMemberName("sampleFile");
	stream.addNumber((int)pThis->v[kParamFile]);

	stream.addMemberName("useSample");
	stream.addNumber((int)pThis->v[kParamUseSample]);
}

bool deserialise(_NT_algorithm* self, _NT_jsonParse& parse)
{
	int numMembers = 0;
	if (!parse.numberOfObjectMembers(numMembers))
		return false;

	for (int i = 0; i < numMembers; ++i)
	{
		if (parse.matchName("sampleFolder"))
		{
			int val = 0;
			if (!parse.number(val)) return false;
			int algIdx = NT_algorithmIndex(self);
			if (algIdx >= 0)
				NT_setParameterFromUi(algIdx, kParamFolder + NT_parameterOffset(), (int16_t)val);
		}
		else if (parse.matchName("sampleFile"))
		{
			int val = 0;
			if (!parse.number(val)) return false;
			int algIdx = NT_algorithmIndex(self);
			if (algIdx >= 0)
				NT_setParameterFromUi(algIdx, kParamFile + NT_parameterOffset(), (int16_t)val);
		}
		else if (parse.matchName("useSample"))
		{
			int val = 0;
			if (!parse.number(val)) return false;
			int algIdx = NT_algorithmIndex(self);
			if (algIdx >= 0)
				NT_setParameterFromUi(algIdx, kParamUseSample + NT_parameterOffset(), (int16_t)val);
		}
		else
		{
			if (!parse.skipMember()) return false;
		}
	}

	return true;
}

// ============================================================
// Factory + entry point
// ============================================================

static const _NT_factory factory =
{
	.guid = NT_MULTICHAR('S', 'r', 'P', 's'),
	.name = "Pulsar",
	.description = "Pulsar synthesis with formants, masking, and CV",
	.numSpecifications = 0,
	.specifications = NULL,
	.calculateStaticRequirements = NULL,
	.initialise = NULL,
	.calculateRequirements = calculateRequirements,
	.construct = construct,
	.parameterChanged = parameterChanged,
	.step = step,
	.draw = draw,
	.midiRealtime = NULL,
	.midiMessage = midiMessage,
	.tags = kNT_tagInstrument,
	.hasCustomUi = NULL,
	.customUi = NULL,
	.setupUi = NULL,
	.serialise = serialise,
	.deserialise = deserialise,
	.midiSysEx = NULL,
	.parameterUiPrefix = NULL,
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
