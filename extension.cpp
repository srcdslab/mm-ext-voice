/**
 * vim: set ts=4 sw=4 tw=99 noet :
 * ======================================================
 * Metamod:Source Voice Plugin
 * Written by AlliedModders LLC.
 * ======================================================
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * This Voice plugin is public domain.
 */

// Base
#include <stdio.h>
#include <inttypes.h>

// HL2SDK
#include "extension.h"
#include "iserver.h"

// Extension
#include "voice/voice.h"
#include "detour/functions.h"

SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK4_void(IServerGameClients, ClientActive, SH_NOATTRIB, 0, CPlayerSlot, bool, const char *, uint64);
SH_DECL_HOOK5_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, CPlayerSlot, int, const char *, uint64, const char *);
SH_DECL_HOOK4_void(IServerGameClients, ClientPutInServer, SH_NOATTRIB, 0, CPlayerSlot, char const *, int, uint64);
SH_DECL_HOOK1_void(IServerGameClients, ClientSettingsChanged, SH_NOATTRIB, 0, CPlayerSlot);
SH_DECL_HOOK6_void(IServerGameClients, OnClientConnected, SH_NOATTRIB, 0, CPlayerSlot, const char *, uint64, const char *, const char *, bool);
SH_DECL_HOOK6(IServerGameClients, ClientConnect, SH_NOATTRIB, 0, bool, CPlayerSlot, const char *, uint64, const char *, bool, CBufferString *);
SH_DECL_HOOK2(IGameEventManager2, FireEvent, SH_NOATTRIB, 0, bool, IGameEvent *, bool);
SH_DECL_HOOK3_void(INetworkServerService, StartupServer, SH_NOATTRIB, 0, const GameSessionConfiguration_t &, ISource2WorldSession *, const char *);

SH_DECL_HOOK2_void(IServerGameClients, ClientCommand, SH_NOATTRIB, 0, CPlayerSlot, const CCommand &);

VoicePlugin g_VoicePlugin;
IServerGameDLL *server = NULL;
IServerGameClients *gameclients = NULL;
IVEngineServer *engine = NULL;
IGameEventManager2 *gameevents = NULL;
IFileSystem *filesystem = NULL;
ICvar *icvar = NULL;

CGlobalVars *gpGlobals = NULL;

Voice *g_Voice = NULL;

// Should only be called within the active game loop (i e map should be loaded and active)
// otherwise that'll be nullptr!
CGlobalVars *GetGameGlobals()
{
	INetworkGameServer *server = g_pNetworkServerService->GetIGameServer();

	if (!server)
		return nullptr;

	return server->GetGlobals();
}

#if 0
// Currently unavailable, requires hl2sdk work!
ConVar sample_cvar("sample_cvar", "42", 0);
#endif

PLUGIN_EXPOSE(Voice, g_VoicePlugin);
bool VoicePlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_CURRENT(GetEngineFactory, icvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, server, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_ANY(GetServerFactory, gameclients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, filesystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	// Currently doesn't work
	// GET_V_IFACE_CURRENT(GetServerFactory, hltvdirector, IHLTVDirector, INTERFACEVERSION_HLTVDIRECTOR);

	// Currently doesn't work from within mm side, use GetGameGlobals() in the mean time instead
	// gpGlobals = ismm->GetCGlobals();
	gpGlobals = GetGameGlobals();

	META_CONPRINTF("Starting plugin.\n");

	SH_ADD_HOOK_MEMFUNC(IServerGameDLL, GameFrame, server, this, &VoicePlugin::Hook_GameFrame, true);
	SH_ADD_HOOK_MEMFUNC(IServerGameClients, ClientActive, gameclients, this, &VoicePlugin::Hook_ClientActive, true);
	SH_ADD_HOOK_MEMFUNC(IServerGameClients, ClientDisconnect, gameclients, this, &VoicePlugin::Hook_ClientDisconnect, true);
	SH_ADD_HOOK_MEMFUNC(IServerGameClients, ClientPutInServer, gameclients, this, &VoicePlugin::Hook_ClientPutInServer, true);
	SH_ADD_HOOK_MEMFUNC(IServerGameClients, ClientSettingsChanged, gameclients, this, &VoicePlugin::Hook_ClientSettingsChanged, false);
	SH_ADD_HOOK_MEMFUNC(IServerGameClients, OnClientConnected, gameclients, this, &VoicePlugin::Hook_OnClientConnected, false);
	SH_ADD_HOOK_MEMFUNC(IServerGameClients, ClientConnect, gameclients, this, &VoicePlugin::Hook_ClientConnect, false);
	SH_ADD_HOOK_MEMFUNC(IServerGameClients, ClientCommand, gameclients, this, &VoicePlugin::Hook_ClientCommand, false);
	SH_ADD_HOOK_MEMFUNC(INetworkServerService, StartupServer, g_pNetworkServerService, this, &VoicePlugin::Hook_StartupServer, true);

	META_CONPRINTF("All hooks started!\n");

	g_pCVar = icvar;
	ConVar_Register(FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL);

	if (!Detour_Register(error, maxlen))
	{
		META_CONPRINTF("[VOICE] [ERROR] %s\n", error);
		return false;
	}

	g_Voice = new Voice();

	if (!g_Voice->Init(error, maxlen))
	{
		META_CONPRINTF("[VOICE] [ERROR] %s\n", error);
		return false;
	}

	return true;
}

bool VoicePlugin::Unload(char *error, size_t maxlen)
{
	SH_REMOVE_HOOK_MEMFUNC(IServerGameDLL, GameFrame, server, this, &VoicePlugin::Hook_GameFrame, true);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, ClientActive, gameclients, this, &VoicePlugin::Hook_ClientActive, true);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, ClientDisconnect, gameclients, this, &VoicePlugin::Hook_ClientDisconnect, true);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, ClientPutInServer, gameclients, this, &VoicePlugin::Hook_ClientPutInServer, true);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, ClientSettingsChanged, gameclients, this, &VoicePlugin::Hook_ClientSettingsChanged, false);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, OnClientConnected, gameclients, this, &VoicePlugin::Hook_OnClientConnected, false);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, ClientConnect, gameclients, this, &VoicePlugin::Hook_ClientConnect, false);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, ClientCommand, gameclients, this, &VoicePlugin::Hook_ClientCommand, false);
	SH_REMOVE_HOOK_MEMFUNC(INetworkServerService, StartupServer, g_pNetworkServerService, this, &VoicePlugin::Hook_StartupServer, true);

	delete g_Voice;

	return true;
}

void VoicePlugin::AllPluginsLoaded()
{
	/* This is where we'd do stuff that relies on the mod or other plugins
	 * being initialized (for example, cvars added and events registered).
	 */
	META_CONPRINTF("All plugins loaded\n");

	char error[256];
	int maxlen = sizeof(error);
	if (!g_Voice->OnAllPluginsLoaded(error, maxlen))
	{
		META_CONPRINTF("[VOICE] [ERROR] %s\n", error);
		this->Unload(error, maxlen);
		return;
	}
}

void VoicePlugin::Hook_StartupServer(const GameSessionConfiguration_t &config, ISource2WorldSession *, const char *)
{
	META_CONPRINTF("Startup server\n");

	gpGlobals = GetGameGlobals();

	if (gpGlobals == nullptr)
	{
		META_CONPRINTF("Failed to lookup gpGlobals\n");
	}
}

void VoicePlugin::Hook_ClientActive(CPlayerSlot slot, bool bLoadGame, const char *pszName, uint64 xuid)
{
	META_CONPRINTF("Hook_ClientActive(%d, %d, \"%s\", %d)\n", slot, bLoadGame, pszName, xuid);
}

void VoicePlugin::Hook_ClientCommand(CPlayerSlot slot, const CCommand &args)
{
	META_CONPRINTF("Hook_ClientCommand(%d, \"%s\")\n", slot, args.GetCommandString());
}

void VoicePlugin::Hook_ClientSettingsChanged(CPlayerSlot slot)
{
	META_CONPRINTF("Hook_ClientSettingsChanged(%d)\n", slot);
}

void VoicePlugin::Hook_OnClientConnected(CPlayerSlot slot, const char *pszName, uint64 xuid, const char *pszNetworkID, const char *pszAddress, bool bFakePlayer)
{
	META_CONPRINTF("Hook_OnClientConnected(%d, \"%s\", %d, \"%s\", \"%s\", %d)\n", slot, pszName, xuid, pszNetworkID, pszAddress, bFakePlayer);
}

bool VoicePlugin::Hook_ClientConnect(CPlayerSlot slot, const char *pszName, uint64 xuid, const char *pszNetworkID, bool unk1, CBufferString *pRejectReason)
{
	META_CONPRINTF("Hook_ClientConnect(%d, \"%s\", %d, \"%s\", %d, \"%s\")\n", slot, pszName, xuid, pszNetworkID, unk1, pRejectReason->ToGrowable()->Get());

	RETURN_META_VALUE(MRES_IGNORED, true);
}

void VoicePlugin::Hook_ClientPutInServer(CPlayerSlot slot, char const *pszName, int type, uint64 xuid)
{
	META_CONPRINTF("Hook_ClientPutInServer(%d, \"%s\", %d, %d, %d)\n", slot, pszName, type, xuid);
}

void VoicePlugin::Hook_ClientDisconnect(CPlayerSlot slot, int reason, const char *pszName, uint64 xuid, const char *pszNetworkID)
{
	META_CONPRINTF("Hook_ClientDisconnect(%d, %d, \"%s\", %d, \"%s\")\n", slot, reason, pszName, xuid, pszNetworkID);
}

void VoicePlugin::Hook_GameFrame(bool simulating, bool bFirstTick, bool bLastTick)
{
	/**
	 * simulating:
	 * ***********
	 * true  | game is ticking
	 * false | game is not ticking
	 */
	if (g_Voice != NULL)
		g_Voice->OnGameFrame(simulating);
}

// Potentially might not work
void VoicePlugin::OnLevelInit(char const *pMapName,
							  char const *pMapEntities,
							  char const *pOldLevel,
							  char const *pLandmarkName,
							  bool loadGame,
							  bool background)
{
	META_CONPRINTF("OnLevelInit(%s)\n", pMapName);
}

// Potentially might not work
void VoicePlugin::OnLevelShutdown()
{
	META_CONPRINTF("OnLevelShutdown()\n");
}

bool VoicePlugin::Pause(char *error, size_t maxlen)
{
	return true;
}

bool VoicePlugin::Unpause(char *error, size_t maxlen)
{
	return true;
}

const char *VoicePlugin::GetLicense()
{
	return "Public Domain";
}

const char *VoicePlugin::GetVersion()
{
	return "1.0.0";
}

const char *VoicePlugin::GetDate()
{
	return __DATE__;
}

const char *VoicePlugin::GetLogTag()
{
	return "VOICE";
}

const char *VoicePlugin::GetAuthor()
{
	return "maxime1907";
}

const char *VoicePlugin::GetDescription()
{
	return "Inject voice data over existing clients";
}

const char *VoicePlugin::GetName()
{
	return "Voice";
}

const char *VoicePlugin::GetURL()
{
	return "https://github.com/srcdslab/mm-ext-voice";
}
