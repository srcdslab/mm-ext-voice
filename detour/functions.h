#ifndef _INCLUDE_METAMOD_SOURCE_DETOUR_FUNCTION_H_
#define _INCLUDE_METAMOD_SOURCE_DETOUR_FUNCTION_H_

#include <stdio.h>
#include <inttypes.h>

#include <ISmmPlugin.h>
#include <igameevents.h>
#include <iplayerinfo.h>
#include <sh_vector.h>

// Protobuf
#include "cs2/netmessages.pb.h"

extern ISmmAPI *g_SMAPI;
extern ISmmPlugin *g_PLAPI;
extern CGlobalVars *gpGlobals;

class IClient;
class INetworkGameServer;
class CBaseEntity;
class CCommand;

using SV_BroadcastVoiceData = void (*)(INetworkGameServer *, IClient *, CMsgVoiceAudio *, uint64_t);
using Host_Say = void (*)(CBaseEntity *, CCommand *, bool, int, const char *);

bool Detour_Register(char *error, size_t maxlen);
void SendConsoleChat(const char *msg, ...);

void PrintCMsgVoiceAudio(CMsgVoiceAudio *pAudio);

#endif //_INCLUDE_METAMOD_SOURCE_VOICE_H_
