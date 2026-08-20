#include "stdafx.h"
#include "Vst3Host.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/gui/iplugview.h"

#include <new>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

namespace Vst3Detail {

using namespace Steinberg;
using namespace Steinberg::Vst;

enum { VST3_MAX_EVENTS = 256, VST3_MAX_BUSES = 64, VST3_BLOCK = 512 };
// Drum machines and multi-out samplers (Groove Agent has 32 output buses) can
// route voices away from the master bus, so every bus is summed into the mix.
enum { VST3_MAX_MIX_BUSES = 32 };

static wchar_t g_vst3Fail[256];

static void Vst3Fail(const wchar_t* why)
{
	g_vst3Fail[0] = 0;
	if (why) wcsncpy_s(g_vst3Fail, why, _TRUNCATE);
}

static int IidEqual(const TUID a, const TUID b)
{
	return memcmp(a, b, sizeof(TUID)) == 0;
}


// A real attribute list, not a stub. Plug-ins built from two components talk
// to themselves through host messages, and the ones that carry their program
// state that way stay mute if the attributes are silently dropped.
class AttrList : public IAttributeList {
public:
	enum { MAX_ATTRS = 64, MAX_STR = 256 };
	explicit AttrList(bool heapOwned = false)
		: refs(1), count(0), owned(heapOwned ? 1 : 0)
	{
		ZeroMemory(items, sizeof(items));
	}
	~AttrList()
	{
		for (int i = 0; i < count; ++i) if (items[i].bin) free(items[i].bin);
	}
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj)
	{
		if (!obj) return kInvalidArgument;
		*obj = NULL;
		if (IidEqual(iid, FUnknown_iid) || IidEqual(iid, IAttributeList_iid)) {
			*obj = static_cast<IAttributeList*>(this);
			addRef();
			return kResultOk;
		}
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef() { return (uint32)InterlockedIncrement(&refs); }
	uint32 PLUGIN_API release()
	{
		const LONG n = InterlockedDecrement(&refs);
		if (n == 0 && owned) delete this;
		return (uint32)n;
	}
	tresult PLUGIN_API setInt(AttrID id, int64 v)
	{
		Item* it = Find(id, true); if (!it) return kResultFalse;
		it->kind = KIND_INT; it->i = v; return kResultOk;
	}
	tresult PLUGIN_API getInt(AttrID id, int64& v)
	{
		Item* it = Find(id, false);
		if (!it || it->kind != KIND_INT) return kResultFalse;
		v = it->i; return kResultOk;
	}
	tresult PLUGIN_API setFloat(AttrID id, double v)
	{
		Item* it = Find(id, true); if (!it) return kResultFalse;
		it->kind = KIND_FLOAT; it->f = v; return kResultOk;
	}
	tresult PLUGIN_API getFloat(AttrID id, double& v)
	{
		Item* it = Find(id, false);
		if (!it || it->kind != KIND_FLOAT) return kResultFalse;
		v = it->f; return kResultOk;
	}
	tresult PLUGIN_API setString(AttrID id, const TChar* s)
	{
		Item* it = Find(id, true); if (!it || !s) return kResultFalse;
		it->kind = KIND_STR;
		int n = 0;
		while (s[n] && n < MAX_STR - 1) { it->str[n] = s[n]; ++n; }
		it->str[n] = 0;
		return kResultOk;
	}
	tresult PLUGIN_API getString(AttrID id, TChar* out, uint32 bytes)
	{
		Item* it = Find(id, false);
		if (!it || it->kind != KIND_STR || !out) return kResultFalse;
		const uint32 room = bytes / sizeof(TChar);
		if (!room) return kResultFalse;
		uint32 n = 0;
		while (it->str[n] && n + 1 < room) { out[n] = it->str[n]; ++n; }
		out[n] = 0;
		return kResultOk;
	}
	tresult PLUGIN_API setBinary(AttrID id, const void* data, uint32 bytes)
	{
		Item* it = Find(id, true);
		if (!it || (!data && bytes)) return kResultFalse;
		if (it->bin) { free(it->bin); it->bin = NULL; it->binBytes = 0; }
		if (bytes) {
			it->bin = malloc(bytes);
			if (!it->bin) return kOutOfMemory;
			memcpy(it->bin, data, bytes);
			it->binBytes = bytes;
		}
		it->kind = KIND_BIN;
		return kResultOk;
	}
	tresult PLUGIN_API getBinary(AttrID id, const void*& data, uint32& bytes)
	{
		Item* it = Find(id, false);
		if (!it || it->kind != KIND_BIN) return kResultFalse;
		data = it->bin; bytes = it->binBytes; return kResultOk;
	}
private:
	enum { KIND_NONE = 0, KIND_INT, KIND_FLOAT, KIND_STR, KIND_BIN };
	struct Item {
		char id[128];
		int kind;
		int64 i;
		double f;
		TChar str[MAX_STR];
		void* bin;
		uint32 binBytes;
	};
	Item* Find(AttrID id, bool create)
	{
		if (!id) return NULL;
		for (int i = 0; i < count; ++i)
			if (strcmp(items[i].id, id) == 0) return &items[i];
		if (!create || count >= MAX_ATTRS) return NULL;
		Item* it = &items[count];
		strncpy_s(it->id, id, _TRUNCATE);
		it->kind = KIND_NONE; it->bin = NULL; it->binBytes = 0;
		++count;
		return it;
	}
	volatile LONG refs;
	int count;
	int owned;
	Item items[MAX_ATTRS];
};

class HostMessage : public IMessage {
public:
	HostMessage() : refs(1) { id[0] = 0; }
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj)
	{
		if (!obj) return kInvalidArgument;
		*obj = NULL;
		if (IidEqual(iid, FUnknown_iid) || IidEqual(iid, IMessage_iid)) {
			*obj = static_cast<IMessage*>(this);
			addRef();
			return kResultOk;
		}
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef() { return (uint32)InterlockedIncrement(&refs); }
	uint32 PLUGIN_API release()
	{
		const LONG n = InterlockedDecrement(&refs);
		if (n == 0) delete this;
		return (uint32)n;
	}
	FIDString PLUGIN_API getMessageID() { return id; }
	void PLUGIN_API setMessageID(FIDString s)
	{
		id[0] = 0;
		if (s) strncpy_s(id, s, _TRUNCATE);
	}
	IAttributeList* PLUGIN_API getAttributes() { return &attrs; }
private:
	volatile LONG refs;
	char id[128];
	AttrList attrs;
};

class MemStream : public IBStream {
public:
	MemStream() : refs(1), data(NULL), size(0), cap(0), pos(0) {}
	~MemStream() { if (data) free(data); }
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj)
	{
		if (!obj) return kInvalidArgument;
		*obj = NULL;
		if (IidEqual(iid, FUnknown_iid) || IidEqual(iid, IBStream_iid)) {
			*obj = static_cast<IBStream*>(this);
			addRef();
			return kResultOk;
		}
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef() { return (uint32)InterlockedIncrement(&refs); }
	uint32 PLUGIN_API release() { return (uint32)InterlockedDecrement(&refs); }
	tresult PLUGIN_API read(void* buffer, int32 numBytes, int32* numRead)
	{
		if (numRead) *numRead = 0;
		if (!buffer || numBytes < 0) return kInvalidArgument;
		int32 n = numBytes;
		if (pos + n > size) n = size - pos;
		if (n < 0) n = 0;
		if (n) memcpy(buffer, data + pos, (size_t)n);
		pos += n;
		if (numRead) *numRead = n;
		return kResultOk;
	}
	tresult PLUGIN_API write(void* buffer, int32 numBytes, int32* numWritten)
	{
		if (numWritten) *numWritten = 0;
		if (!buffer || numBytes < 0) return kInvalidArgument;
		if (pos + numBytes > cap) {
			int32 nc = cap ? cap * 2 : 4096;
			while (nc < pos + numBytes) nc *= 2;
			BYTE* nd = (BYTE*)realloc(data, (size_t)nc);
			if (!nd) return kOutOfMemory;
			data = nd; cap = nc;
		}
		memcpy(data + pos, buffer, (size_t)numBytes);
		pos += numBytes;
		if (pos > size) size = pos;
		if (numWritten) *numWritten = numBytes;
		return kResultOk;
	}
	tresult PLUGIN_API seek(int64 p, int32 mode, int64* result)
	{
		int64 np = pos;
		if (mode == kIBSeekSet) np = p;
		else if (mode == kIBSeekCur) np = pos + p;
		else if (mode == kIBSeekEnd) np = size + p;
		if (np < 0) np = 0;
		pos = (int32)np;
		if (result) *result = pos;
		return kResultOk;
	}
	tresult PLUGIN_API tell(int64* p)
	{
		if (!p) return kInvalidArgument;
		*p = pos;
		return kResultOk;
	}
	void rewind() { pos = 0; }
private:
	volatile LONG refs;
	BYTE* data;
	int32 size, cap, pos;
};

class CompHandler : public IComponentHandler, public IUnitHandler {
public:
	CompHandler() : refs(1) {}
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj)
	{
		if (!obj) return kInvalidArgument;
		*obj = NULL;
		if (IidEqual(iid, FUnknown_iid) || IidEqual(iid, IComponentHandler_iid)) {
			*obj = static_cast<IComponentHandler*>(this);
			addRef();
			return kResultOk;
		}
		if (IidEqual(iid, IUnitHandler_iid)) {
			*obj = static_cast<IUnitHandler*>(this);
			addRef();
			return kResultOk;
		}
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef() { return (uint32)InterlockedIncrement(&refs); }
	uint32 PLUGIN_API release() { return (uint32)InterlockedDecrement(&refs); }
	tresult PLUGIN_API beginEdit(ParamID) { return kResultOk; }
	tresult PLUGIN_API performEdit(ParamID, ParamValue) { return kResultOk; }
	tresult PLUGIN_API endEdit(ParamID) { return kResultOk; }
	tresult PLUGIN_API restartComponent(int32) { return kResultOk; }
	tresult PLUGIN_API notifyUnitSelection(UnitID) { return kResultOk; }
	tresult PLUGIN_API notifyProgramListChange(ProgramListID, int32) { return kResultOk; }
private:
	volatile LONG refs;
};

// The plug-in asks for its window to be resized through this, which VST3
// editors do as soon as they switch page or scale.
class PlugFrame : public Steinberg::IPlugFrame {
public:
	PlugFrame() : refs(1), wnd(NULL) {}
	void SetWindow(HWND h) { wnd = h; }
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj)
	{
		if (!obj) return kInvalidArgument;
		*obj = NULL;
		if (IidEqual(iid, FUnknown_iid) || IidEqual(iid, Steinberg::IPlugFrame_iid)) {
			*obj = static_cast<Steinberg::IPlugFrame*>(this);
			addRef();
			return kResultOk;
		}
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef() { return (uint32)InterlockedIncrement(&refs); }
	uint32 PLUGIN_API release() { return (uint32)InterlockedDecrement(&refs); }
	tresult PLUGIN_API resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* r)
	{
		if (!r) return kInvalidArgument;
		if (wnd) {
			RECT wr = { 0, 0, r->right - r->left, r->bottom - r->top };
			AdjustWindowRect(&wr, (DWORD)GetWindowLongW(wnd, GWL_STYLE), FALSE);
			SetWindowPos(wnd, NULL, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
				SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
		if (view) view->onSize(r);
		return kResultOk;
	}
private:
	volatile LONG refs;
	HWND wnd;
};

class HostApplication : public IHostApplication {
public:
	HostApplication() : refs(1) {}

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj)
	{
		if (!obj) return kInvalidArgument;
		*obj = NULL;
		if (IidEqual(iid, FUnknown_iid) || IidEqual(iid, IHostApplication_iid)) {
			*obj = static_cast<IHostApplication*>(this);
			addRef();
			return kResultOk;
		}
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef() { return (uint32)InterlockedIncrement(&refs); }
	uint32 PLUGIN_API release() { return (uint32)InterlockedDecrement(&refs); }

	tresult PLUGIN_API getName(String128 name)
	{
		if (!name) return kInvalidArgument;
		static const char text[] = "ogg VST3 Host";
		int i = 0;
		for (; text[i] && i < 127; ++i) name[i] = (char16)text[i];
		name[i] = 0;
		return kResultOk;
	}
	tresult PLUGIN_API createInstance(TUID cid, TUID iid, void** obj)
	{
		if (!obj) return kInvalidArgument;
		*obj = NULL;
		if (IidEqual(cid, IMessage_iid) && IidEqual(iid, IMessage_iid)) {
			*obj = new HostMessage();
			return kResultOk;
		}
		if (IidEqual(cid, IAttributeList_iid) && IidEqual(iid, IAttributeList_iid)) {
			*obj = new AttrList(true);
			return kResultOk;
		}
		return kNoInterface;
	}

private:
	volatile LONG refs;
};

class FixedEventList : public IEventList {
public:
	FixedEventList() : refs(1), count(0) {}

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj)
	{
		if (!obj) return kInvalidArgument;
		*obj = NULL;
		if (IidEqual(iid, FUnknown_iid) || IidEqual(iid, IEventList_iid)) {
			*obj = static_cast<IEventList*>(this);
			addRef();
			return kResultOk;
		}
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef() { return (uint32)InterlockedIncrement(&refs); }
	uint32 PLUGIN_API release() { return (uint32)InterlockedDecrement(&refs); }
	int32 PLUGIN_API getEventCount() { return count; }
	tresult PLUGIN_API getEvent(int32 index, Event& e)
	{
		if (index < 0 || index >= count) return kInvalidArgument;
		e = events[index];
		return kResultOk;
	}
	tresult PLUGIN_API addEvent(Event& e)
	{
		if (count >= VST3_MAX_EVENTS) return kOutOfMemory;
		events[count++] = e;
		return kResultOk;
	}
	void clear() { count = 0; }

	Event events[VST3_MAX_EVENTS];
	int32 count;

private:
	volatile LONG refs;
};

class ParamQueue : public IParamValueQueue {
public:
	ParamQueue() : refs(1), id(0), n(0) {}
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj)
	{
		if (!obj) return kInvalidArgument;
		*obj = NULL;
		if (IidEqual(iid, FUnknown_iid) || IidEqual(iid, IParamValueQueue_iid)) {
			*obj = static_cast<IParamValueQueue*>(this);
			addRef();
			return kResultOk;
		}
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef() { return (uint32)InterlockedIncrement(&refs); }
	uint32 PLUGIN_API release() { return (uint32)InterlockedDecrement(&refs); }
	ParamID PLUGIN_API getParameterId() { return id; }
	int32 PLUGIN_API getPointCount() { return n; }
	tresult PLUGIN_API getPoint(int32 index, int32& sampleOffset, ParamValue& value)
	{
		if (index < 0 || index >= n) return kInvalidArgument;
		sampleOffset = off[index];
		value = val[index];
		return kResultOk;
	}
	tresult PLUGIN_API addPoint(int32 sampleOffset, ParamValue value, int32& index)
	{
		if (n >= 8) return kOutOfMemory;
		off[n] = sampleOffset;
		val[n] = value;
		index = n++;
		return kResultOk;
	}
	void reset(ParamID pid) { id = pid; n = 0; }

	ParamID id;
	int32 n;
	int32 off[8];
	ParamValue val[8];
private:
	volatile LONG refs;
};

class HostParamChanges : public IParameterChanges {
public:
	HostParamChanges() : refs(1), nq(0) {}
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj)
	{
		if (!obj) return kInvalidArgument;
		*obj = NULL;
		if (IidEqual(iid, FUnknown_iid) || IidEqual(iid, IParameterChanges_iid)) {
			*obj = static_cast<IParameterChanges*>(this);
			addRef();
			return kResultOk;
		}
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef() { return (uint32)InterlockedIncrement(&refs); }
	uint32 PLUGIN_API release() { return (uint32)InterlockedDecrement(&refs); }
	int32 PLUGIN_API getParameterCount() { return nq; }
	IParamValueQueue* PLUGIN_API getParameterData(int32 index)
	{
		if (index < 0 || index >= nq) return NULL;
		return &q[index];
	}
	IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id, int32& index)
	{
		for (int32 i = 0; i < nq; ++i) if (q[i].id == id) { index = i; return &q[i]; }
		if (nq >= 16) { index = -1; return NULL; }
		index = nq;
		q[nq].reset(id);
		return &q[nq++];
	}
	void clear() { nq = 0; }

	ParamQueue q[16];
	int32 nq;
private:
	volatile LONG refs;
};

class EmptyParameterChanges : public IParameterChanges {
public:
	EmptyParameterChanges() : refs(1) {}
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj)
	{
		if (!obj) return kInvalidArgument;
		*obj = NULL;
		if (IidEqual(iid, FUnknown_iid) || IidEqual(iid, IParameterChanges_iid)) {
			*obj = static_cast<IParameterChanges*>(this);
			addRef();
			return kResultOk;
		}
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef() { return (uint32)InterlockedIncrement(&refs); }
	uint32 PLUGIN_API release() { return (uint32)InterlockedDecrement(&refs); }
	int32 PLUGIN_API getParameterCount() { return 0; }
	IParamValueQueue* PLUGIN_API getParameterData(int32) { return NULL; }
	IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32& index)
	{
		index = -1;
		return NULL;
	}
private:
	volatile LONG refs;
};

static int IsDirectory(const wchar_t* path)
{
	const DWORD a = GetFileAttributesW(path);
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int ResolveModulePath(const wchar_t* source, wchar_t* dll, int chars)
{
	if (!source || !*source || !dll || chars <= 0) return 0;
	dll[0] = 0;
	if (!IsDirectory(source)) {
		wcsncpy_s(dll, chars, source, _TRUNCATE);
		return GetFileAttributesW(dll) != INVALID_FILE_ATTRIBUTES;
	}
#ifdef _WIN64
	const wchar_t* platform = L"Contents\\x86_64-win\\*.vst3";
#else
	const wchar_t* platform = L"Contents\\x86-win\\*.vst3";
#endif
	wchar_t pattern[1024] = {};
	wcsncpy_s(pattern, source, _TRUNCATE);
	const size_t n = wcslen(pattern);
	if (n && pattern[n - 1] != L'\\') wcscat_s(pattern, L"\\");
	wcscat_s(pattern, platform);
	WIN32_FIND_DATAW fd = {};
	HANDLE find = FindFirstFileW(pattern, &fd);
	if (find == INVALID_HANDLE_VALUE) return 0;
	FindClose(find);
	wcsncpy_s(dll, chars, pattern, _TRUNCATE);
	wchar_t* slash = wcsrchr(dll, L'\\');
	if (!slash) return 0;
	wcscpy_s(slash + 1, chars - (int)(slash + 1 - dll), fd.cFileName);
	return 1;
}

typedef IPluginFactory* (PLUGIN_API *GetFactoryProc)();
typedef bool (PLUGIN_API *InitDllProc)();
typedef bool (PLUGIN_API *ExitDllProc)();

} // namespace Vst3Detail

struct Vst3Inst {
	HMODULE module;
	Steinberg::IPluginFactory* factory;
	Steinberg::Vst::IComponent* component;
	Steinberg::Vst::IAudioProcessor* processor;
	Steinberg::Vst::IEditController* controller;
	Steinberg::Vst::IMidiMapping* midiMap;
	Steinberg::Vst::IUnitInfo* units;
	Steinberg::Vst::ParamID progParam;
	Steinberg::Vst::ProgramListID progListId;
	int progCount;
	int hasProgParam;
	int midiInCh;
	Vst3Detail::HostApplication host;
	Vst3Detail::CompHandler handler;
	Vst3Detail::PlugFrame frame;
	Steinberg::IPlugView* view;
	Vst3Detail::FixedEventList pending;
	Vst3Detail::FixedEventList blockEvents;
	Vst3Detail::HostParamChanges params;
	Vst3Detail::EmptyParameterChanges outParams;
	Steinberg::Vst::ProcessContext ctx;
	int ok;
	int outputChannels;
	int bus0Channels;
	int audioIns;
	int audioOuts;
	int mixOutBuses;
	int initialized;
	int ctrlInit;
	int connected;
	int active;
	int processing;
	__int64 samplePos;
	float* extraBufs; // (mixOutBuses-1) x 2 x VST3_BLOCK, summed into the mix

	Vst3Inst()
		: module(NULL), factory(NULL), component(NULL), processor(NULL),
		  controller(NULL), midiMap(NULL), units(NULL),
		  progParam(0), progListId(-1), progCount(0), hasProgParam(0), midiInCh(1),
		  view(NULL), ok(0), outputChannels(2), bus0Channels(2), audioIns(0), audioOuts(0), mixOutBuses(1),
		  initialized(0), ctrlInit(0),
		  connected(0), active(0), processing(0), samplePos(0), extraBufs(NULL)
	{
		ZeroMemory(&ctx, sizeof(ctx));
	}
};

Vst3Inst* Vst3Open(const wchar_t* vst3PathOrDll)
{
	using namespace Steinberg;
	using namespace Steinberg::Vst;
	using namespace Vst3Detail;
	wchar_t modulePath[1024] = {};
	if (!ResolveModulePath(vst3PathOrDll, modulePath, 1024)) return NULL;

	Vst3Inst* v = new (std::nothrow) Vst3Inst;
	if (!v) return NULL;
	v->module = LoadLibraryW(modulePath);
	if (!v->module) { Vst3Fail(L"LoadLibrary"); Vst3Close(v); return NULL; }

	InitDllProc initDll = (InitDllProc)GetProcAddress(v->module, "InitDll");
	if (initDll && !initDll()) { Vst3Fail(L"InitDll"); Vst3Close(v); return NULL; }

	GetFactoryProc getFactory =
		(GetFactoryProc)GetProcAddress(v->module, "GetPluginFactory");
	if (!getFactory) { Vst3Fail(L"GetPluginFactory"); Vst3Close(v); return NULL; }
	v->factory = getFactory();
	if (!v->factory) { Vst3Fail(L"factory null"); Vst3Close(v); return NULL; }
	IPluginFactory3* factory3 = NULL;
	if (v->factory->queryInterface(IPluginFactory3_iid,
		(void**)&factory3) == kResultOk && factory3) {
		factory3->setHostContext(&v->host);
		factory3->release();
	}

	TUID selected = {};
	int haveClass = 0;
	int haveInstrument = 0;
	IPluginFactory2* factory2 = NULL;
	if (v->factory->queryInterface(IPluginFactory2_iid,
		(void**)&factory2) == kResultOk && factory2) {
		for (int32 i = 0; i < v->factory->countClasses(); ++i) {
			PClassInfo2 ci;
			if (factory2->getClassInfo2(i, &ci) != kResultOk ||
				strcmp(ci.category, kVstAudioEffectClass) != 0) continue;
			const int instrument = strstr(ci.subCategories, "Instrument") != NULL;
			if (!haveClass || (instrument && !haveInstrument)) {
				memcpy(selected, ci.cid, sizeof(TUID));
				haveClass = 1;
				haveInstrument = instrument;
			}
		}
		factory2->release();
	} else {
		for (int32 i = 0; i < v->factory->countClasses(); ++i) {
			PClassInfo ci;
			if (v->factory->getClassInfo(i, &ci) == kResultOk &&
				strcmp(ci.category, kVstAudioEffectClass) == 0) {
				memcpy(selected, ci.cid, sizeof(TUID));
				haveClass = 1;
				break;
			}
		}
	}
	if (!haveClass) { Vst3Fail(L"no audio class"); Vst3Close(v); return NULL; }

	if (v->factory->createInstance(selected, IComponent_iid,
		(void**)&v->component) != kResultOk || !v->component) {
		Vst3Fail(L"createComponent"); Vst3Close(v); return NULL;
	}
	if (v->component->queryInterface(IAudioProcessor_iid,
		(void**)&v->processor) != kResultOk || !v->processor) {
		Vst3Fail(L"no IAudioProcessor"); Vst3Close(v); return NULL;
	}
	v->component->setIoMode(kSimple);
	if (v->component->initialize(&v->host) != kResultOk) {
		Vst3Fail(L"initialize"); Vst3Close(v); return NULL;
	}
	v->initialized = 1;

	// Steinberg synths keep default patch on the edit controller. Without
	// connecting it, oscillators/volume stay at 0 and MIDI probe is silent.
	if (v->component->queryInterface(IEditController_iid,
		(void**)&v->controller) == kResultOk && v->controller) {
		v->controller->setComponentHandler(&v->handler);
	} else {
		v->controller = NULL;
		TUID ctrlCid = {};
		if (v->component->getControllerClassId(ctrlCid) == kResultOk) {
			v->factory->createInstance(ctrlCid, IEditController_iid,
				(void**)&v->controller);
		}
		if (v->controller) {
			if (v->controller->initialize(&v->host) == kResultOk)
				v->ctrlInit = 1;
			v->controller->setComponentHandler(&v->handler);
			Steinberg::Vst::IConnectionPoint* cpComp = NULL;
			Steinberg::Vst::IConnectionPoint* cpCtrl = NULL;
			v->component->queryInterface(Steinberg::Vst::IConnectionPoint_iid, (void**)&cpComp);
			v->controller->queryInterface(Steinberg::Vst::IConnectionPoint_iid, (void**)&cpCtrl);
			if (cpComp && cpCtrl && cpComp != cpCtrl) {
				cpComp->connect(cpCtrl);
				cpCtrl->connect(cpComp);
				v->connected = 1;
			}
			if (cpComp) cpComp->release();
			if (cpCtrl) cpCtrl->release();
			MemStream st;
			if (v->component->getState(&st) == kResultOk) {
				st.rewind();
				v->controller->setComponentState(&st);
			}
		}
	}

	const int32 audioIns = v->component->getBusCount(kAudio, kInput);
	const int32 audioOuts = v->component->getBusCount(kAudio, kOutput);
	if (audioOuts < 1) {
		Vst3Fail(L"no audio out bus"); Vst3Close(v); return NULL;
	}
	const int32 nIn = audioIns > VST3_MAX_BUSES ? VST3_MAX_BUSES : audioIns;
	const int32 nOut = audioOuts > VST3_MAX_BUSES ? VST3_MAX_BUSES : audioOuts;
	SpeakerArrangement inArr[VST3_MAX_BUSES];
	SpeakerArrangement outArr[VST3_MAX_BUSES];
	for (int i = 0; i < VST3_MAX_BUSES; ++i) {
		inArr[i] = SpeakerArr::kStereo;
		outArr[i] = SpeakerArr::kStereo;
	}
	for (int32 i = 0; i < nIn; ++i)
		v->processor->getBusArrangement(kInput, i, inArr[i]);
	for (int32 i = 0; i < nOut; ++i)
		v->processor->getBusArrangement(kOutput, i, outArr[i]);
	if (audioIns <= VST3_MAX_BUSES && audioOuts <= VST3_MAX_BUSES)
		v->processor->setBusArrangements(inArr, nIn, outArr, nOut);

	BusInfo outInfo = {};
	if (v->component->getBusInfo(kAudio, kOutput, 0, outInfo) != kResultOk) {
		Vst3Fail(L"getBusInfo out0"); Vst3Close(v); return NULL;
	}
	if (outInfo.channelCount < 1) {
		Vst3Fail(L"out0 has 0 ch"); Vst3Close(v); return NULL;
	}
	v->bus0Channels = outInfo.channelCount;
	if (v->bus0Channels > 16) v->bus0Channels = 16;
	v->outputChannels = (v->bus0Channels >= 2) ? 2 : 1;
	v->audioIns = audioIns;
	v->audioOuts = audioOuts;
	v->mixOutBuses = audioOuts;
	if (v->mixOutBuses < 1) v->mixOutBuses = 1;
	if (v->mixOutBuses > VST3_MAX_MIX_BUSES) v->mixOutBuses = VST3_MAX_MIX_BUSES;
	if (v->mixOutBuses > 1) {
		v->extraBufs = (float*)calloc((size_t)(v->mixOutBuses - 1) * 2 * VST3_BLOCK,
			sizeof(float));
		if (!v->extraBufs) v->mixOutBuses = 1;
	}

	ProcessSetup setup = {};
	setup.processMode = kRealtime;
	setup.symbolicSampleSize = kSample32;
	setup.maxSamplesPerBlock = VST3_BLOCK;
	setup.sampleRate = 44100.0;
	if (v->processor->canProcessSampleSize(kSample32) != kResultTrue ||
		v->processor->setupProcessing(setup) != kResultOk) {
		Vst3Fail(L"setupProcessing"); Vst3Close(v); return NULL;
	}
	for (int32 i = 0; i < v->component->getBusCount(kEvent, kInput); ++i)
		v->component->activateBus(kEvent, kInput, i, true);
	for (int32 i = 0; i < audioIns; ++i)
		v->component->activateBus(kAudio, kInput, i, false);
	for (int32 i = 0; i < audioOuts; ++i)
		v->component->activateBus(kAudio, kOutput, i, i < v->mixOutBuses);
	if (v->component->setActive(true) != kResultOk) {
		Vst3Fail(L"setActive"); Vst3Close(v); return NULL;
	}
	v->active = 1;
	if (v->processor->setProcessing(true) != kResultOk) {
		Vst3Fail(L"setProcessing"); Vst3Close(v); return NULL;
	}
	v->processing = 1;
	if (v->controller)
		v->controller->queryInterface(IMidiMapping_iid, (void**)&v->midiMap);
	v->midiInCh = 1;
	{
		BusInfo ev = {};
		if (v->component->getBusInfo(kEvent, kInput, 0, ev) == kResultOk &&
			ev.channelCount > 0)
			v->midiInCh = ev.channelCount;
	}
	if (v->controller) {
		v->controller->queryInterface(IUnitInfo_iid, (void**)&v->units);
		if (v->units) {
			const int32 nlist = v->units->getProgramListCount();
			for (int32 li = 0; li < nlist; ++li) {
				ProgramListInfo pli = {};
				if (v->units->getProgramListInfo(li, pli) != kResultOk) continue;
				if (pli.programCount > v->progCount) {
					v->progCount = pli.programCount;
					v->progListId = pli.id;
				}
			}
		}
		const int32 np = v->controller->getParameterCount();
		for (int32 i = 0; i < np; ++i) {
			ParameterInfo pi = {};
			if (v->controller->getParameterInfo(i, pi) != kResultOk) continue;
			if (pi.flags & ParameterInfo::kIsProgramChange) {
				v->progParam = pi.id;
				v->hasProgParam = 1;
				const int pc = (pi.stepCount > 0) ? (pi.stepCount + 1) : 0;
				if (pc > v->progCount) v->progCount = pc;
				break;
			}
		}
	}
	v->ok = 1;
	Vst3Fail(L"");
	return v;
}

void Vst3Close(Vst3Inst* v)
{
	using namespace Steinberg;
	using namespace Steinberg::Vst;
	using namespace Vst3Detail;
	if (!v) return;
	v->ok = 0;
	Vst3EditorClose(v);
	if (v->processor && v->processing) {
		v->processor->setProcessing(false);
		v->processing = 0;
	}
	if (v->component && v->active) {
		v->component->setActive(false);
		v->active = 0;
	}
	if (v->connected && v->component && v->controller) {
		Steinberg::Vst::IConnectionPoint* cpComp = NULL;
		Steinberg::Vst::IConnectionPoint* cpCtrl = NULL;
		v->component->queryInterface(Steinberg::Vst::IConnectionPoint_iid, (void**)&cpComp);
		v->controller->queryInterface(Steinberg::Vst::IConnectionPoint_iid, (void**)&cpCtrl);
		if (cpComp && cpCtrl) {
			cpComp->disconnect(cpCtrl);
			cpCtrl->disconnect(cpComp);
		}
		if (cpComp) cpComp->release();
		if (cpCtrl) cpCtrl->release();
		v->connected = 0;
	}
	if (v->controller && v->ctrlInit) {
		v->controller->terminate();
		v->ctrlInit = 0;
	}
	if (v->component && v->initialized) {
		v->component->terminate();
		v->initialized = 0;
	}
	if (v->units) { v->units->release(); v->units = NULL; }
	if (v->midiMap) { v->midiMap->release(); v->midiMap = NULL; }
	if (v->controller) { v->controller->release(); v->controller = NULL; }
	if (v->processor) { v->processor->release(); v->processor = NULL; }
	if (v->component) { v->component->release(); v->component = NULL; }
	if (v->factory) { v->factory->release(); v->factory = NULL; }
	if (v->module) {
		ExitDllProc exitDll = (ExitDllProc)GetProcAddress(v->module, "ExitDll");
		if (exitDll) exitDll();
		FreeLibrary(v->module);
		v->module = NULL;
	}
	if (v->extraBufs) { free(v->extraBufs); v->extraBufs = NULL; }
	delete v;
}

static void MapMidiCc(Vst3Inst* v, Steinberg::int16 channel,
	Steinberg::Vst::CtrlNumber cn, Steinberg::Vst::ParamValue val, int sampleOffset)
{
	using namespace Steinberg;
	using namespace Steinberg::Vst;
	if (!v || !v->midiMap) return;
	ParamID id = 0;
	if (v->midiMap->getMidiControllerAssignment(0, channel, cn, id) != kResultOk)
		return;
	if (v->controller) v->controller->setParamNormalized(id, val);
	int32 qi = 0;
	IParamValueQueue* q = v->params.addParameterData(id, qi);
	if (!q) return;
	int32 pi = 0;
	q->addPoint(sampleOffset < 0 ? 0 : sampleOffset, val, pi);
}

void Vst3MidiShort(Vst3Inst* v, DWORD msg, int sampleOffset)
{
	using namespace Steinberg;
	using namespace Steinberg::Vst;
	using namespace Vst3Detail;
	if (!v || !v->ok) return;
	Event e = {};
	e.busIndex = 0;
	e.sampleOffset = sampleOffset < 0 ? 0 : sampleOffset;
	e.flags = 0;
	const int status = msg & 0xff;
	const int type = status & 0xf0;
	const int channel = status & 15;
	const int d1 = (msg >> 8) & 0x7f;
	const int d2 = (msg >> 16) & 0x7f;
	if (type == 0xb0)
		MapMidiCc(v, (int16)channel, (CtrlNumber)d1, d2 / 127.0, e.sampleOffset);
	else if (type == 0xc0)
		MapMidiCc(v, (int16)channel, kCtrlProgramChange, d1 / 127.0, e.sampleOffset);
	else if (type == 0xd0)
		MapMidiCc(v, (int16)channel, kAfterTouch, d1 / 127.0, e.sampleOffset);
	else if (type == 0xe0)
		MapMidiCc(v, (int16)channel, kPitchBend,
			((d2 << 7) | d1) / 16383.0, e.sampleOffset);
	if (v->pending.count >= VST3_MAX_EVENTS) return;
	if (type == 0x90 && d2) {
		e.type = Event::kNoteOnEvent;
		e.noteOn.channel = (int16)channel;
		e.noteOn.pitch = (int16)d1;
		e.noteOn.tuning = 0;
		e.noteOn.velocity = d2 / 127.0f;
		e.noteOn.length = 0;
		// -1 means "no id", so the plug-in matches note-off by channel and
		// pitch. Using the pitch as an id collides between MIDI channels on a
		// multi-timbral instance, where a note-off then releases the wrong
		// voice and the original one keeps sounding.
		e.noteOn.noteId = -1;
	} else if (type == 0x80 || type == 0x90) {
		e.type = Event::kNoteOffEvent;
		e.noteOff.channel = (int16)channel;
		e.noteOff.pitch = (int16)d1;
		e.noteOff.velocity = d2 / 127.0f;
		e.noteOff.noteId = -1;
		e.noteOff.tuning = 0;
	} else if (type == 0xa0) {
		e.type = Event::kPolyPressureEvent;
		e.polyPressure.channel = (int16)channel;
		e.polyPressure.pitch = (int16)d1;
		e.polyPressure.pressure = d2 / 127.0f;
		e.polyPressure.noteId = -1;
	} else {
		e.type = Event::kLegacyMIDICCOutEvent;
		e.midiCCOut.channel = (int8)channel;
		e.midiCCOut.value2 = (int8)d2;
		if (type == 0xb0) {
			e.midiCCOut.controlNumber = (uint8)d1;
			e.midiCCOut.value = (int8)d2;
		} else if (type == 0xc0) {
			e.midiCCOut.controlNumber = (uint8)ControllerNumbers::kCtrlProgramChange;
			e.midiCCOut.value = (int8)d1;
		} else if (type == 0xd0) {
			e.midiCCOut.controlNumber = (uint8)ControllerNumbers::kAfterTouch;
			e.midiCCOut.value = (int8)d1;
		} else if (type == 0xe0) {
			e.midiCCOut.controlNumber = (uint8)ControllerNumbers::kPitchBend;
			e.midiCCOut.value = (int8)d1;
		} else {
			return;
		}
	}
	v->pending.addEvent(e);
}

int Vst3EditorOpen(Vst3Inst* v, void* parentHwnd, int* outW, int* outH)
{
	using namespace Steinberg;
	using namespace Steinberg::Vst;
	using namespace Vst3Detail;
	if (!v || !v->ok || !v->controller || !parentHwnd) return -1;
	if (v->view) return 0;
	IPlugView* view = NULL;
	__try { view = v->controller->createView(ViewType::kEditor); }
	__except (EXCEPTION_EXECUTE_HANDLER) { view = NULL; }
	if (!view) { Vst3Fail(L"createView"); return -2; }
	tresult rc = kResultFalse;
	__try {
		if (view->isPlatformTypeSupported(kPlatformTypeHWND) == kResultTrue) {
			v->frame.SetWindow((HWND)parentHwnd);
			view->setFrame(&v->frame);
			rc = view->attached(parentHwnd, kPlatformTypeHWND);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { rc = kResultFalse; }
	if (rc != kResultOk) {
		Vst3Fail(L"attached");
		__try { view->setFrame(NULL); view->release(); }
		__except (EXCEPTION_EXECUTE_HANDLER) {}
		return -3;
	}
	ViewRect r = {};
	__try {
		if (view->getSize(&r) != kResultOk) r.right = r.bottom = 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { r.right = r.bottom = 0; }
	if (outW) *outW = (r.right > r.left) ? (r.right - r.left) : 640;
	if (outH) *outH = (r.bottom > r.top) ? (r.bottom - r.top) : 480;
	v->view = view;
	return 0;
}

void Vst3EditorClose(Vst3Inst* v)
{
	if (!v || !v->view) return;
	Steinberg::IPlugView* view = v->view;
	v->view = NULL;
	__try {
		view->removed();
		view->setFrame(NULL);
		view->release();
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {}
	v->frame.SetWindow(NULL);
}

void Vst3Process(Vst3Inst* v, float* outL, float* outR, int frames)
{
	using namespace Steinberg;
	using namespace Steinberg::Vst;
	using namespace Vst3Detail;
	if (!outL || !outR || frames <= 0) return;
	if (!v || !v->ok || !v->processor) {
		ZeroMemory(outL, frames * sizeof(float));
		ZeroMemory(outR, frames * sizeof(float));
		return;
	}
	int done = 0;
	while (done < frames) {
		int count = frames - done;
		if (count > VST3_BLOCK) count = VST3_BLOCK;
		ZeroMemory(outL + done, count * sizeof(float));
		ZeroMemory(outR + done, count * sizeof(float));

		v->blockEvents.clear();
		int keep = 0;
		for (int i = 0; i < v->pending.count; ++i) {
			Event e = v->pending.events[i];
			if (e.sampleOffset < count) {
				v->blockEvents.addEvent(e);
			} else {
				e.sampleOffset -= count;
				v->pending.events[keep++] = e;
			}
		}
		v->pending.count = keep;

		float zin[2][VST3_BLOCK];
		ZeroMemory(zin, sizeof(zin));
		float* inCh[2] = { zin[0], zin[1] };
		AudioBusBuffers input = {};
		input.numChannels = 2;
		input.silenceFlags = 3;
		input.channelBuffers32 = inCh;

		float dump[16][VST3_BLOCK];
		ZeroMemory(dump, sizeof(dump));
		int nch = v->bus0Channels > 0 ? v->bus0Channels : 2;
		if (nch > 16) nch = 16;
		float* channels[16];
		channels[0] = outL + done;
		channels[1] = (nch >= 2) ? (outR + done) : dump[1];
		for (int c = 2; c < nch; ++c) channels[c] = dump[c];
		AudioBusBuffers output[VST3_MAX_MIX_BUSES];
		ZeroMemory(output, sizeof(output));
		output[0].numChannels = nch;
		output[0].silenceFlags = 0;
		output[0].channelBuffers32 = channels;
		float* extraCh[VST3_MAX_MIX_BUSES - 1][2];
		ZeroMemory(extraCh, sizeof(extraCh));
		const int nOut = (v->mixOutBuses > 1 && v->extraBufs) ? v->mixOutBuses : 1;
		if (nOut > 1)
			ZeroMemory(v->extraBufs,
				(size_t)(nOut - 1) * 2 * VST3_BLOCK * sizeof(float));
		for (int b = 1; b < nOut; ++b) {
			extraCh[b - 1][0] = v->extraBufs + (size_t)(b - 1) * 2 * VST3_BLOCK;
			extraCh[b - 1][1] = extraCh[b - 1][0] + VST3_BLOCK;
			output[b].numChannels = 2;
			output[b].silenceFlags = 0;
			output[b].channelBuffers32 = extraCh[b - 1];
		}

		v->ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid |
			ProcessContext::kTimeSigValid | ProcessContext::kContTimeValid;
		v->ctx.sampleRate = 44100.0;
		v->ctx.projectTimeSamples = v->samplePos;
		v->ctx.continousTimeSamples = v->samplePos;
		v->ctx.tempo = 120.0;
		v->ctx.timeSigNumerator = 4;
		v->ctx.timeSigDenominator = 4;

		ProcessData data = {};
		data.processMode = kRealtime;
		data.symbolicSampleSize = kSample32;
		data.numSamples = count;
		data.numInputs = (v->audioIns > 0) ? 1 : 0;
		data.numOutputs = nOut;
		data.inputs = (v->audioIns > 0) ? &input : NULL;
		data.outputs = output;
		data.inputParameterChanges = &v->params;
		data.outputParameterChanges = &v->outParams;
		data.inputEvents = &v->blockEvents;
		data.processContext = &v->ctx;
		if (v->processor->process(data) != kResultOk) {
			ZeroMemory(outL + done, count * sizeof(float));
			ZeroMemory(outR + done, count * sizeof(float));
			v->ok = 0;
		}
		v->params.clear();
		if (nOut > 1) {
			for (int b = 1; b < nOut; ++b) {
				const float* xl = extraCh[b - 1][0];
				const float* xr = extraCh[b - 1][1];
				for (int i = 0; i < count; ++i) {
					outL[done + i] += xl[i];
					outR[done + i] += xr[i];
				}
			}
		}
		if (nch >= 3) {
			for (int i = 0; i < count; ++i) {
				for (int c = 2; c < nch; ++c) {
					outL[done + i] += dump[c][i] * 0.35f;
					outR[done + i] += dump[c][i] * 0.35f;
				}
			}
		}
		if (nch == 1)
			memcpy(outR + done, outL + done, count * sizeof(float));
		v->samplePos += count;
		done += count;
	}
}

int Vst3IsOk(Vst3Inst* v)
{
	return v && v->ok;
}

int Vst3MidiChannels(Vst3Inst* v)
{
	return v ? v->midiInCh : 0;
}

int Vst3ProgramCount(Vst3Inst* v)
{
	return v ? v->progCount : 0;
}

int Vst3ProgramName(Vst3Inst* v, int index, wchar_t* out, int outChars)
{
	using namespace Steinberg;
	using namespace Steinberg::Vst;
	if (out && outChars > 0) out[0] = 0;
	if (!v || !v->ok || index < 0 || !out || outChars <= 0) return 0;
	if (v->units && v->progListId != kNoProgramListId && index < v->progCount) {
		String128 name = {};
		if (v->units->getProgramName(v->progListId, index, name) == kResultOk) {
			int i = 0;
			for (; i < outChars - 1 && i < 128 && name[i]; ++i)
				out[i] = (wchar_t)name[i];
			out[i] = 0;
			return out[0] ? 1 : 0;
		}
	}
	_snwprintf_s(out, outChars, _TRUNCATE, L"Program %d", index);
	return 1;
}

int Vst3SetProgram(Vst3Inst* v, int index)
{
	using namespace Steinberg;
	using namespace Steinberg::Vst;
	if (!v || !v->ok || index < 0) return 0;
	if (v->hasProgParam && v->controller) {
		const int den = (v->progCount > 1) ? (v->progCount - 1) : 1;
		ParamValue nv = (ParamValue)index / (ParamValue)den;
		if (nv < 0) nv = 0;
		if (nv > 1) nv = 1;
		IEditControllerHostEditing* hostEdit = NULL;
		v->controller->queryInterface(IEditControllerHostEditing_iid, (void**)&hostEdit);
		if (hostEdit) hostEdit->beginEditFromHost(v->progParam);
		v->controller->setParamNormalized(v->progParam, nv);
		if (hostEdit) {
			hostEdit->endEditFromHost(v->progParam);
			hostEdit->release();
		}
		int32 qi = 0;
		IParamValueQueue* q = v->params.addParameterData(v->progParam, qi);
		if (q) {
			int32 pi = 0;
			q->addPoint(0, nv, pi);
		}
		return 1;
	}
	if (index <= 127)
		Vst3MidiShort(v, 0xc0 | ((index & 0x7f) << 8), 0);
	return 1;
}

int Vst3SetChannelProgram(Vst3Inst* v, int midiCh, int index)
{
	using namespace Steinberg;
	using namespace Steinberg::Vst;
	if (!v || !v->ok) return 0;
	if (midiCh < 0) midiCh = 0;
	if (midiCh > 15) midiCh = 15;
	if (v->units) {
		UnitID uid = 0;
		if (v->units->getUnitByBus(kEvent, kInput, 0, midiCh, uid) == kResultOk)
			v->units->selectUnit(uid);
	}
	Vst3SetProgram(v, index);
	if (index >= 0 && index <= 127)
		Vst3MidiShort(v, (0xc0 | midiCh) | ((index & 0x7f) << 8), 0);
	return 1;
}

const wchar_t* Vst3LastError()
{
	return Vst3Detail::g_vst3Fail;
}
