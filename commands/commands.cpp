#include "detour/functions.h"
#include "voice/voice.h"

#include <ISmmPlugin.h>
#include <igameevents.h>
#include <iplayerinfo.h>
#include <sh_vector.h>

extern ISmmAPI *g_SMAPI;
extern ISmmPlugin *g_PLAPI;
extern IVEngineServer *engine;

extern Voice *g_Voice;

#ifndef MAX_PLAYERS
#define MAX_PLAYERS 65
#endif

CON_COMMAND_F(mm_talking, "List of players talking", FCVAR_NONE)
{
	SendConsoleChat("Players talking:");
	for (int client = 0; client < MAX_PLAYERS; client++)
	{
		if (g_Voice != NULL && g_Voice->IsClientSpeaking(client))
			SendConsoleChat("%d", client);
	}
}

CON_COMMAND_F(voice_command, "Voice command", FCVAR_NONE)
{
	META_CONPRINTF("[VOICE] Voice command called by %d. Command: %s\n", context.GetPlayerSlot(), args.GetCommandString());
	// SV_BroadcastVoiceData svBroadcastVoiceData = reinterpret_cast<SV_BroadcastVoiceData>(g_pSV_BroadcastVoiceData);
	// svBroadcastVoiceData(nullptr, nullptr, nullptr, context.GetPlayerSlot().Get());
}

CON_COMMAND_F(mm_map, "Change map", FCVAR_NONE)
{
	if (args.ArgC() < 2)
		return;

	const char *map = args[1];
	if (engine->IsMapValid(map))
	{
		SendConsoleChat("Changing map to %s", map);
		META_CONPRINTF("[VOICE] Changing map to %s\n", map);

		engine->ChangeLevel(map, NULL);
	}
	else
	{
		SendConsoleChat("Map %s is not valid", map);
	}
}

CON_COMMAND_F(mm_respawn, "Respawn player", FCVAR_NONE)
{
	SendConsoleChat("Respawning %d", context.GetPlayerSlot());
	META_CONPRINTF("[VOICE] Respawning %d\n", context.GetPlayerSlot());
}
