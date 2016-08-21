#include "GarrysMod/Lua/Interface.h"

#include "bass.h"
#include "windows.h"
#include <string>
#include "biquad.c"

using namespace GarrysMod;

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
BASSFunc_ChannelGetInfo BassGetInfo;

#define TYPE_IGMODAUDIOCHANNEL 38

DWORD GetBassHandle(GarrysMod::Lua::UserData* udata) {
	return *((DWORD *)udata->data + 1);
}

int SetPan(lua_State *state) {
	auto udata = (GarrysMod::Lua::UserData*) LUA->GetUserdata(1);
	if (udata->type != TYPE_IGMODAUDIOCHANNEL) {
		LUA->ArgError(1, "must be a IGModAudioChannel");
	}

	double pan = LUA->CheckNumber(2);
	BassSetAttrib(GetBassHandle(udata), BASS_ATTRIB_PAN, (float) pan);

	return 0;
}

int SetReverb(lua_State *state) {
	auto udata = (GarrysMod::Lua::UserData*) LUA->GetUserdata(1);
	if (udata->type != TYPE_IGMODAUDIOCHANNEL) {
		LUA->ArgError(1, "must be a IGModAudioChannel");
	}

	BassSetFX(GetBassHandle(udata), BASS_FX_DX8_REVERB, 0);

	return 0;
}

int SetParamEQ(lua_State *state) {
	auto udata = (GarrysMod::Lua::UserData*) LUA->GetUserdata(1);
	if (udata->type != TYPE_IGMODAUDIOCHANNEL) {
		LUA->ArgError(1, "must be a IGModAudioChannel");
	}

	double center = LUA->CheckNumber(2);
	double bandwidth = LUA->CheckNumber(3);
	double gain = LUA->CheckNumber(4);

	HFX handle =  BassSetFX(GetBassHandle(udata), BASS_FX_DX8_PARAMEQ, 0);

	BASS_DX8_PARAMEQ params;
	params.fCenter = (float)center;
	params.fBandwidth = (float)bandwidth;
	params.fGain = (float)gain;

	BassSetFXParams(handle, &params);

	return 0;
}

void CALLBACK BiquadDSP(HDSP handle, DWORD channel, void *buffer, DWORD length, void *user)
{
	biquad* b = (biquad*)user;

	short *s = (short*) buffer;
	for (; length; length -= 2, s += 1) {
		s[0] = (short) (BiQuad(((double)s[0]) * (1.0 / 32767.0), b) * 32767);
	}
}

int SetLowpass(lua_State *state) {
	auto udata = (GarrysMod::Lua::UserData*) LUA->GetUserdata(1);
	if (udata->type != TYPE_IGMODAUDIOCHANNEL) {
		LUA->ArgError(1, "must be a IGModAudioChannel");
	}

	double freq = LUA->CheckNumber(2);
	double resonance = LUA->CheckNumber(3);

	BassSetDSP(GetBassHandle(udata), &BiquadDSP, BiQuad_new(LPF, 0, freq, 44100.0, resonance), 0);

	return 0;
}

int SetEAXParameters(lua_State *state) {
	auto udata = (GarrysMod::Lua::UserData*) LUA->GetUserdata(1);
	if (udata->type != TYPE_IGMODAUDIOCHANNEL) {
		LUA->ArgError(1, "must be a IGModAudioChannel");
	}

	BassSetEAXParameters(EAX_PRESET_HANGAR);
	//BassSetFX(GetBassHandle(udata), BASS_FX_DX8_CHORUS, 0);

	return 0;
}


GMOD_MODULE_OPEN() {

	HMODULE hBASS = GetModuleHandle(TEXT("bass.dll"));
	/*BassSetFX = (BASSFunc_ChannelSetFX)GetProcAddress(hBASS, "BASS_ChannelSetFX");
	if (!BassSetFX) {
		return 0;
	}*/
	BassSetFX = (BASSFunc_ChannelSetFX)GetProcAddress(hBASS, "BASS_ChannelSetFX");
	BassSetDSP = (BASSFunc_ChannelSetDSP)GetProcAddress(hBASS, "BASS_ChannelSetDSP");
	BassSetFXParams = (BASSFunc_FXSetParameters)GetProcAddress(hBASS, "BASS_FXSetParameters");
	BassSetEAXParameters = (BASSFunc_SetEAXParameters)GetProcAddress(hBASS, "BASS_SetEAXParameters");
	BassGetInfo = (BASSFunc_ChannelGetInfo)GetProcAddress(hBASS, "BASS_ChannelGetInfo");
	BassSetAttrib = (BASSFunc_ChannelSetAttribute)GetProcAddress(hBASS, "BASS_ChannelSetAttribute");
	if (!BassSetFX || !BassSetDSP || !BassSetFXParams || !BassSetEAXParameters || !BassGetInfo || !BassSetAttrib) {
		return 0;
	}

	LUA->PushSpecial(Lua::SPECIAL_REG);
	LUA->GetField(-1, "IGModAudioChannel");

	LUA->PushString("SetPan");
	LUA->PushCFunction(SetPan);
	LUA->SetTable(-3);

	LUA->PushString("SetReverb");
	LUA->PushCFunction(SetReverb);
	LUA->SetTable(-3);

	LUA->PushString("SetParamEQ");
	LUA->PushCFunction(SetParamEQ);
	LUA->SetTable(-3);

	LUA->PushString("SetLowpass");
	LUA->PushCFunction(SetLowpass);
	LUA->SetTable(-3);

	LUA->PushString("SetEAXParameters");
	LUA->PushCFunction(SetEAXParameters);
	LUA->SetTable(-3);

	LUA->Pop();

	return 0;
}
