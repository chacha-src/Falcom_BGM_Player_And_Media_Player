/*
 * Minimal Steinberg VST 2.4 ABI declarations.
 * This is an ABI compatibility header, not the Steinberg SDK.
 */
#ifndef OGG_THIRD_PARTY_VST2_AEFFECT_H
#define OGG_THIRD_PARTY_VST2_AEFFECT_H

#include <stdint.h>

#if defined(_WIN32)
# define VSTCALLBACK __cdecl
#else
# define VSTCALLBACK
#endif

typedef int32_t VstInt32;
typedef int16_t VstInt16;
#if INTPTR_MAX == INT64_MAX
typedef int64_t VstIntPtr;
#else
typedef int32_t VstIntPtr;
#endif

struct AEffect;
struct VstEvents;

typedef VstIntPtr (VSTCALLBACK *audioMasterCallback)(
	struct AEffect*, VstInt32 opcode, VstInt32 index,
	VstIntPtr value, void* ptr, float opt);
typedef VstIntPtr (VSTCALLBACK *AEffectDispatcherProc)(
	struct AEffect*, VstInt32 opcode, VstInt32 index,
	VstIntPtr value, void* ptr, float opt);
typedef void (VSTCALLBACK *AEffectProcessProc)(
	struct AEffect*, float** inputs, float** outputs, VstInt32 sampleFrames);
typedef void (VSTCALLBACK *AEffectProcessDoubleProc)(
	struct AEffect*, double** inputs, double** outputs, VstInt32 sampleFrames);
typedef void (VSTCALLBACK *AEffectSetParameterProc)(
	struct AEffect*, VstInt32 index, float parameter);
typedef float (VSTCALLBACK *AEffectGetParameterProc)(
	struct AEffect*, VstInt32 index);

enum { kEffectMagic = 0x56737450 }; /* 'VstP' */

typedef struct AEffect {
	VstInt32 magic;
	AEffectDispatcherProc dispatcher;
	AEffectProcessProc process;
	AEffectSetParameterProc setParameter;
	AEffectGetParameterProc getParameter;
	VstInt32 numPrograms;
	VstInt32 numParams;
	VstInt32 numInputs;
	VstInt32 numOutputs;
	VstInt32 flags;
	VstIntPtr resvd1;
	VstIntPtr resvd2;
	VstInt32 initialDelay;
	VstInt32 realQualities;
	VstInt32 offQualities;
	float ioRatio;
	void* object;
	void* user;
	VstInt32 uniqueID;
	VstInt32 version;
	AEffectProcessProc processReplacing;
	AEffectProcessDoubleProc processDoubleReplacing;
	char future[56];
} AEffect;

enum VstAEffectFlags {
	effFlagsHasEditor       = 1 << 0,
	effFlagsCanReplacing    = 1 << 4,
	effFlagsProgramChunks   = 1 << 5,
	effFlagsIsSynth         = 1 << 8,
	effFlagsNoSoundInStop   = 1 << 9,
	effFlagsCanDoubleReplacing = 1 << 12
};

enum AEffectOpcodes {
	effOpen = 0,
	effClose,
	effSetProgram,
	effGetProgram,
	effSetProgramName,
	effGetProgramName,
	effGetParamLabel,
	effGetParamDisplay,
	effGetParamName,
	effGetVu,
	effSetSampleRate,
	effSetBlockSize,
	effMainsChanged,
	effEditGetRect,
	effEditOpen,
	effEditClose,
	effEditDraw,
	effEditMouse,
	effEditKey,
	effEditIdle,
	effEditTop,
	effEditSleep,
	effIdentify,
	effGetChunk,
	effSetChunk,
	effProcessEvents,
	effCanBeAutomated,
	effString2Parameter,
	effGetNumProgramCategories,
	effGetProgramNameIndexed,
	effCopyProgram,
	effConnectInput,
	effConnectOutput,
	effGetInputProperties,
	effGetOutputProperties,
	effGetPlugCategory,
	effGetCurrentPosition,
	effGetDestinationBuffer,
	effOfflineNotify,
	effOfflinePrepare,
	effOfflineRun,
	effProcessVarIo,
	effSetSpeakerArrangement,
	effSetBlockSizeAndSampleRate,
	effSetBypass,
	effGetEffectName,
	effGetErrorText,
	effGetVendorString,
	effGetProductString,
	effGetVendorVersion,
	effVendorSpecific,
	effCanDo,
	effGetTailSize,
	effIdle,
	effGetIcon,
	effSetViewPosition,
	effGetParameterProperties,
	effKeysRequired,
	effGetVstVersion,
	effEditKeyDown,
	effEditKeyUp,
	effSetEditKnobMode,
	effGetMidiProgramName,
	effGetCurrentMidiProgram,
	effGetMidiProgramCategory,
	effHasMidiProgramsChanged,
	effGetMidiKeyName,
	effBeginSetProgram,
	effEndSetProgram,
	effGetSpeakerArrangement,
	effShellGetNextPlugin,
	effStartProcess,
	effStopProcess,
	effSetTotalSampleToProcess,
	effSetPanLaw,
	effBeginLoadBank,
	effBeginLoadProgram,
	effSetProcessPrecision,
	effGetNumMidiInputChannels,
	effGetNumMidiOutputChannels
};

enum AudioMasterOpcodes {
	audioMasterAutomate = 0,
	audioMasterVersion,
	audioMasterCurrentId,
	audioMasterIdle,
	audioMasterPinConnected,
	audioMasterWantMidi,
	audioMasterGetTime,
	audioMasterProcessEvents,
	audioMasterSetTime,
	audioMasterTempoAt,
	audioMasterGetNumAutomatableParameters,
	audioMasterGetParameterQuantization,
	audioMasterIOChanged,
	audioMasterNeedIdle,
	audioMasterSizeWindow,
	audioMasterGetSampleRate,
	audioMasterGetBlockSize,
	audioMasterGetInputLatency,
	audioMasterGetOutputLatency,
	audioMasterGetPreviousPlug,
	audioMasterGetNextPlug,
	audioMasterWillReplaceOrAccumulate,
	audioMasterGetCurrentProcessLevel,
	audioMasterGetAutomationState,
	audioMasterOfflineStart,
	audioMasterOfflineRead,
	audioMasterOfflineWrite,
	audioMasterOfflineGetCurrentPass,
	audioMasterOfflineGetCurrentMetaPass,
	audioMasterSetOutputSampleRate,
	audioMasterGetOutputSpeakerArrangement,
	audioMasterGetVendorString,
	audioMasterGetProductString,
	audioMasterGetVendorVersion,
	audioMasterVendorSpecific,
	audioMasterSetIcon,
	audioMasterCanDo,
	audioMasterGetLanguage,
	audioMasterOpenWindow,
	audioMasterCloseWindow,
	audioMasterGetDirectory,
	audioMasterUpdateDisplay,
	audioMasterBeginEdit,
	audioMasterEndEdit,
	audioMasterOpenFileSelector,
	audioMasterCloseFileSelector
};

enum VstPlugCategory {
	kPlugCategUnknown = 0,
	kPlugCategEffect,
	kPlugCategSynth,
	kPlugCategAnalysis,
	kPlugCategMastering,
	kPlugCategSpacializer,
	kPlugCategRoomFx,
	kPlugSurroundFx,
	kPlugCategRestoration,
	kPlugCategOfflineProcess,
	kPlugCategShell,
	kPlugCategGenerator
};

enum VstEventTypes {
	kVstMidiType = 1,
	kVstAudioType,
	kVstVideoType,
	kVstParameterType,
	kVstTriggerType,
	kVstSysExType
};

typedef struct VstEvent {
	VstInt32 type;
	VstInt32 byteSize;
	VstInt32 deltaFrames;
	VstInt32 flags;
	char data[16];
} VstEvent;

typedef struct VstMidiEvent {
	VstInt32 type;
	VstInt32 byteSize;
	VstInt32 deltaFrames;
	VstInt32 flags;
	VstInt32 noteLength;
	VstInt32 noteOffset;
	char midiData[4];
	char detune;
	char noteOffVelocity;
	char reserved1;
	char reserved2;
} VstMidiEvent;

typedef struct VstMidiSysexEvent {
	VstInt32 type;
	VstInt32 byteSize;
	VstInt32 deltaFrames;
	VstInt32 flags;
	VstInt32 dumpBytes;
	VstIntPtr resvd1;
	char* sysexDump;
	VstIntPtr resvd2;
} VstMidiSysexEvent;

typedef struct VstEvents {
	VstInt32 numEvents;
	VstIntPtr reserved;
	VstEvent* events[2];
} VstEvents;

enum VstTimeInfoFlags {
	kVstTransportChanged     = 1,
	kVstTransportPlaying     = 1 << 1,
	kVstTransportCycleActive = 1 << 2,
	kVstTransportRecording   = 1 << 3,
	kVstAutomationWriting    = 1 << 6,
	kVstAutomationReading    = 1 << 7,
	kVstNanosValid           = 1 << 8,
	kVstPpqPosValid          = 1 << 9,
	kVstTempoValid           = 1 << 10,
	kVstBarsValid            = 1 << 11,
	kVstCyclePosValid        = 1 << 12,
	kVstTimeSigValid         = 1 << 13,
	kVstSmpteValid           = 1 << 14,
	kVstClockValid           = 1 << 15
};

typedef struct VstTimeInfo {
	double samplePos;
	double sampleRate;
	double nanoSeconds;
	double ppqPos;
	double tempo;
	double barStartPos;
	double cycleStartPos;
	double cycleEndPos;
	VstInt32 timeSigNumerator;
	VstInt32 timeSigDenominator;
	VstInt32 smpteOffset;
	VstInt32 smpteFrameRate;
	VstInt32 samplesToNextClock;
	VstInt32 flags;
} VstTimeInfo;

typedef AEffect* (VSTCALLBACK *VSTPluginMainProc)(audioMasterCallback);

#endif
