#include "GarrysMod/Lua/Interface.h"

#include "windows.h"
#include <string>

#include "bass.h"
#include "biquad.c"
#include "pdll.h"

using namespace GarrysMod;

class BassDLL : public PDLL
{
	DECLARE_CLASS(BassDLL)

	DECLARE_FUNCTION0(int, BASS_ErrorGetCode)
	DECLARE_FUNCTION3(HFX, BASS_ChannelSetFX, DWORD, DWORD, int)
	DECLARE_FUNCTION2(BOOL, BASS_ChannelRemoveFX, DWORD, DWORD)
	DECLARE_FUNCTION2(BOOL, BASS_FXGetParameters, HFX, void*)
	DECLARE_FUNCTION2(BOOL, BASS_FXSetParameters, HFX, const void*)
	DECLARE_FUNCTION3(BOOL, BASS_ChannelSetAttribute, DWORD, DWORD, float)
	DECLARE_FUNCTION4(HDSP, BASS_ChannelSetDSP, DWORD, DSPPROC*, void*, int)
};

/*
typedef HFX(WINAPI *BASSFunc_ChannelSetFX)(DWORD handle, DWORD type, int priority);
typedef HDSP(WINAPI *BASSFunc_ChannelSetDSP)(DWORD handle, DSPPROC *proc, void *user, int priority);
typedef BOOL(WINAPI *BASSFunc_FXSetParameters)(HFX handle, const void *params);
typedef BOOL(WINAPI *BASSFunc_SetEAXParameters)(int env, float vol, float decay, float damp);
typedef BOOL(WINAPI *BASSFunc_ChannelGetInfo)(DWORD handle, BASS_CHANNELINFO* info);
typedef BOOL(WINAPI *BASSFunc_ChannelSetAttribute)(DWORD handle, DWORD attrib, float value);

BASSFunc_ChannelSetFX BassSetFX;
BASSFunc_ChannelSetDSP BassSetDSP;
BASSFunc_FXSetParameters BassSetFXParams;
BASSFunc_SetEAXParameters BassSetEAXParameters;
BASSFunc_ChannelSetAttribute BassSetAttrib;
BASSFunc_ChannelGetInfo BassGetInfo;*/

BassDLL* bassDll;

DWORD GetBassHandle(GarrysMod::Lua::UserData* udata) {
	return *((DWORD *)udata->data + 1);
}

#define TYPE_IGMODAUDIOCHANNEL 38
#define GET_AUDIOCHANNEL(sp) \
	auto budata = (GarrysMod::Lua::UserData*) LUA->GetUserdata(sp); \
	if (budata->type != TYPE_IGMODAUDIOCHANNEL) { \
		LUA->ArgError(1, "must be a IGModAudioChannel"); \
	}\
	DWORD handle = GetBassHandle(budata)

#define THROW_BASS_ERR \
	char buf[50]; \
	sprintf_s(buf, 50, "BASS Error: %d", bassDll->BASS_ErrorGetCode()); \
	LUA->ThrowError(buf);

int SetPan(lua_State *state) {
	GET_AUDIOCHANNEL(1);

	double pan = LUA->CheckNumber(2);
	bassDll->BASS_ChannelSetAttribute(handle, BASS_ATTRIB_PAN, (float)pan);

	return 0;
}

int AddEffect(lua_State *state) {
	GET_AUDIOCHANNEL(1);

	const char* eff = LUA->CheckString(2);

	DWORD effEnum;
	if (strcmp(eff, "reverb") == 0) {
		effEnum = BASS_FX_DX8_REVERB;
	}
	else {
		LUA->ArgError(2, "invalid effect name");
	}

	HFX fx = bassDll->BASS_ChannelSetFX(handle, effEnum, 0);
	if (!fx) {
		THROW_BASS_ERR
		return 0;
	}

	HFX* ptr = new HFX;
	*ptr = fx;
	LUA->PushUserdata(ptr);
	return 1;
}

int ModifyReverb(lua_State *state) {
	GET_AUDIOCHANNEL(1);
	auto h_fx = (HFX*) LUA->GetUserdata(2);

	BASS_DX8_REVERB params;
	if (!bassDll->BASS_FXGetParameters(*h_fx, &params)) {
		THROW_BASS_ERR
		return 0;
	}

	params.fInGain = (float)LUA->IsType(3, Lua::Type::NUMBER) ? LUA->GetNumber(3) : 0.0;
	params.fReverbMix = (float)LUA->IsType(4, Lua::Type::NUMBER) ? LUA->GetNumber(4) : 0.0;
	params.fReverbTime = (float)LUA->IsType(5, Lua::Type::NUMBER) ? LUA->GetNumber(5) : 1000.0;
	params.fHighFreqRTRatio = (float)LUA->IsType(6, Lua::Type::NUMBER) ? LUA->GetNumber(6) : 0.001;

	if (!bassDll->BASS_FXSetParameters(*h_fx, &params)) {
		THROW_BASS_ERR
		return 0;
	}

	return 0;
}

int RemoveEffect(lua_State *state) {
	GET_AUDIOCHANNEL(1);
	auto h_fx = (HFX*)LUA->GetUserdata(2);

	if (!bassDll->BASS_ChannelRemoveFX(handle, *h_fx)) {
		THROW_BASS_ERR
		return 0;
	}
	
	return 0;
}

/*
int SetParamEQ(lua_State *state) {
	auto udata = (GarrysMod::Lua::UserData*) LUA->GetUserdata(1);
	if (udata->type != TYPE_IGMODAUDIOCHANNEL) {
		LUA->ArgError(1, "must be a IGModAudioChannel");
	}

	double center = LUA->CheckNumber(2);
	double bandwidth = LUA->CheckNumber(3);
	double gain = LUA->CheckNumber(4);

	HFX handle = bassDll->BASS_ChannelSetFX(GetBassHandle(udata), BASS_FX_DX8_PARAMEQ, 0);

	BASS_DX8_PARAMEQ params;
	params.fCenter = (float)center;
	params.fBandwidth = (float)bandwidth;
	params.fGain = (float)gain;

	BassSetFXParams(handle, &params);

	return 0;
}*/

struct BQFData {
	HDSP dsp;
	biquad* b;
};

#define MAGIC_CLIPPING_PREVENTION_FACTOR 0.7
#define MAGIC_CLIPPING_PREVENTION_FACTOR_INV (1.0 / MAGIC_CLIPPING_PREVENTION_FACTOR)


void CALLBACK BiquadDSP(HDSP handle, DWORD channel, void *buffer, DWORD length, void *user)
{
	biquad* b = ((BQFData*)user)->b;

	short *s = (short*) buffer;
	for (; length; length -= 2, s += 1) {
		double in = ((double)s[0]) * (1.0 / 32767.0);
		in *= MAGIC_CLIPPING_PREVENTION_FACTOR;

		double res = BiQuad(in, b);
		res *= MAGIC_CLIPPING_PREVENTION_FACTOR_INV;

		res = min(1.0, max(-1.0, res));
	
		s[0] = (short) (res * 32767);
	}
}

int SetLowpass(lua_State *state) {
	auto udata = (GarrysMod::Lua::UserData*) LUA->GetUserdata(1);
	if (udata->type != TYPE_IGMODAUDIOCHANNEL) {
		LUA->ArgError(1, "must be a IGModAudioChannel");
	}

	BQFData* bqfd = new BQFData;
	bqfd->b = BiQuad_new(LPF, 0, 400, 44100.0, 1);

	HDSP dsp = bassDll->BASS_ChannelSetDSP(GetBassHandle(udata), &BiquadDSP, bqfd, 0);

	bqfd->dsp = dsp;
	LUA->PushUserdata(bqfd);
	return 1;
}

int ModifyLowpass(lua_State *state) {
	GET_AUDIOCHANNEL(1);
	auto bqfd = (BQFData*)LUA->GetUserdata(2);

	const char* c = LUA->CheckString(3);
	double gain = LUA->CheckNumber(4);
	double freq = LUA->CheckNumber(5);
	double resonance = LUA->CheckNumber(6);

	bqfd->b = BiQuad_new(LPF, gain, freq, 44100.0, resonance);

	return 0;
}

GMOD_MODULE_OPEN() {
	/*
	hBASS = GetModuleHandle(TEXT("bass.dll"));
	if (!hBASS) {
		return 0;
	}

	BassSetFX = (BASSFunc_ChannelSetFX)GetProcAddress(hBASS, "BASS_ChannelSetFX");
	BassSetDSP = (BASSFunc_ChannelSetDSP)GetProcAddress(hBASS, "BASS_ChannelSetDSP");
	BassSetFXParams = (BASSFunc_FXSetParameters)GetProcAddress(hBASS, "BASS_FXSetParameters");
	BassSetEAXParameters = (BASSFunc_SetEAXParameters)GetProcAddress(hBASS, "BASS_SetEAXParameters");
	BassGetInfo = (BASSFunc_ChannelGetInfo)GetProcAddress(hBASS, "BASS_ChannelGetInfo");
	BassSetAttrib = (BASSFunc_ChannelSetAttribute)GetProcAddress(hBASS, "BASS_ChannelSetAttribute");
	if (!BassSetFX || !BassSetDSP || !BassSetFXParams || !BassSetEAXParameters || !BassGetInfo || !BassSetAttrib) {
		return 0;
	}*/
	bassDll = new BassDLL("bass.dll");

	LUA->PushSpecial(Lua::SPECIAL_REG);
	LUA->GetField(-1, "IGModAudioChannel");

	LUA->PushString("SetPan");
	LUA->PushCFunction(SetPan);
	LUA->SetTable(-3);

	LUA->PushString("AddEffect");
	LUA->PushCFunction(AddEffect);
	LUA->SetTable(-3);

	LUA->PushString("ModifyReverb");
	LUA->PushCFunction(ModifyReverb);
	LUA->SetTable(-3);

	LUA->PushString("AddLowpass");
	LUA->PushCFunction(SetLowpass);
	LUA->SetTable(-3);

	LUA->PushString("ModifyLowpass");
	LUA->PushCFunction(ModifyLowpass);
	LUA->SetTable(-3);


	LUA->PushString("RemoveEffect");
	LUA->PushCFunction(RemoveEffect);
	LUA->SetTable(-3);

	LUA->Pop();

	return 0;
}
