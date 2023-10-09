#include "functions.h"
#include <stdio.h>

SV_BroadcastVoiceData g_pSV_BroadcastVoiceData = NULL;
Host_Say g_pHost_Say = NULL;

#include "voice/voice.h"
#include "utils/utils.h"

// Extension
#include "gameconfig/gameconfig.h"

// DynoHook
#include "pch.h"
#include "dynohook/manager.h"
#include "dynohook/conventions/x64/x64SystemVcall.h"

extern Voice *g_Voice;
extern IVEngineServer *engine;
extern IServerGameDLL *server;
extern IFileSystem *filesystem;

void SendConsoleChat(const char *msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buf[256];
	V_vsnprintf(buf, sizeof(buf), msg, args);

	va_end(args);

	CCommand newArgs;
	newArgs.Tokenize(buf);

	g_pHost_Say(nullptr, &newArgs, false, 0, nullptr);
}

void ParseChatCommand(char *pCommand, CBaseEntity *pEntity)
{
	char *token, *pos;

	pCommand = strdup(pCommand);
	token = strtok_r(pCommand + 1, " ", &pos);

	// if (!V_stricmp(token, "map"))
	// {

	// }

	free(pCommand);
}

void PrintCMsgVoiceAudio(CMsgVoiceAudio *pAudio)
{
	VoiceDataFormat_t format = pAudio->format();
	::google::protobuf::int32 sequence_bytes = pAudio->sequence_bytes();
	::google::protobuf::uint32 section_number = pAudio->section_number();
	::google::protobuf::uint32 sample_rate = pAudio->sample_rate();
	META_CONPRINTF("[VOICE] format: %d, sequence_bytes: %" PRId32 ", section_number: %" PRIu32 ", sample_rate: %" PRIu32 "\n", format, sequence_bytes, section_number, sample_rate);

	std::string hex_value = string_to_hex(pAudio->voice_data().c_str());
	META_CONPRINTF("[VOICE] voice_data: %s\n", hex_value.c_str());
}

dyno::ReturnAction Detour_SV_BroadcastVoiceData(dyno::HookType hookType, dyno::Hook &hook)
{
	META_CONPRINTF("[VOICE] Inside SV_BroadcastVoiceData\n");

	INetworkGameServer *pServer = hook.getArgument<INetworkGameServer *>(0);
	IClient *pClient = hook.getArgument<IClient *>(1);
	CMsgVoiceAudio *pAudio = hook.getArgument<CMsgVoiceAudio *>(2);
	// xuid = Steam64 ID
	uint64_t xuid = hook.getArgument<uint64_t>(3);

	if (pAudio)
	{
		PrintCMsgVoiceAudio(pAudio);
		g_Voice->OnBroadcastVoiceData(pClient, pAudio->voice_data().size(), (char *)pAudio->voice_data().c_str());
	}
	// META_CONPRINTF( "Sending voice from: %s - playerslot: %d [ xuid %llx ]\n", pClient->GetClientName(), pClient->GetPlayerSlot() + 1, xuid );
	META_CONPRINTF("[VOICE] Sending voice [ xuid %" PRIu64 " ]\n", xuid);

	return dyno::ReturnAction::Ignored;
}

dyno::ReturnAction Detour_Host_Say(dyno::HookType hookType, dyno::Hook &hook)
{
	META_CONPRINTF("[VOICE] Inside Host_Say\n");

	CBaseEntity *pEntity = hook.getArgument<CBaseEntity *>(0);
	CCommand *args = hook.getArgument<CCommand *>(1);
	bool teamonly = hook.getArgument<bool>(2);
	int unk1 = hook.getArgument<int>(3);
	const char *unk2 = hook.getArgument<const char *>(4);

	char *pMessage = (char *)args->Arg(1);

	if (*pMessage == '!' || *pMessage == '/')
		ParseChatCommand(pMessage, pEntity);

	if (*pMessage == '/')
		return dyno::ReturnAction::Supercede;

	return dyno::ReturnAction::Handled;
}

bool Detour_Register(char *error, size_t maxlen)
{
	CBufferStringGrowable<256> gamedirpath;
	engine->GetGameDir(gamedirpath);

	std::string gamedirname = getDirectoryName(gamedirpath.Get());

	const char *gamedataPath = "addons/voice/gamedata/voice.games.txt";
	META_CONPRINTF("[VOICE] Loading %s for game: %s\n", gamedataPath, gamedirname.c_str());

	CGameConfig *g_GameConfig = new CGameConfig(gamedirname, gamedataPath);

	char conf_error[255] = "";
	if (!g_GameConfig->Init(filesystem, conf_error, sizeof(conf_error)))
	{
		snprintf(error, maxlen, "Could not read %s: %s", g_GameConfig->GetPath().c_str(), conf_error);
		return false;
	}

	g_pSV_BroadcastVoiceData = reinterpret_cast<SV_BroadcastVoiceData>(g_GameConfig->GetAddress("SV_BroadcastVoiceData", (void *)engine, (void *)server, conf_error, sizeof(conf_error)));
	if (g_pSV_BroadcastVoiceData == NULL)
	{
		snprintf(error, maxlen, "Failed to get SV_BroadcastVoiceData: %s", conf_error);
		return false;
	}

	g_pHost_Say = reinterpret_cast<Host_Say>(g_GameConfig->GetAddress("Host_Say", (void *)engine, (void *)server, conf_error, sizeof(conf_error)));
	if (g_pHost_Say == NULL)
	{
		snprintf(error, maxlen, "Failed to get Host_Say: %s", conf_error);
		return false;
	}

	dyno::HookManager &manager = dyno::HookManager::Get();
	dyno::Hook *hookVoice = manager.hook((void *)g_pSV_BroadcastVoiceData, []
										 { return new dyno::x64SystemVcall({dyno::DataType::Pointer, dyno::DataType::Pointer, dyno::DataType::Pointer, dyno::DataType::Int}, dyno::DataType::Void); });
	hookVoice->addCallback(dyno::HookType::Pre, (dyno::HookHandler *)&Detour_SV_BroadcastVoiceData);

	dyno::Hook *hookSay = manager.hook((void *)g_pHost_Say, []
									   { return new dyno::x64SystemVcall({dyno::DataType::Pointer, dyno::DataType::Pointer, dyno::DataType::Bool, dyno::DataType::Int, dyno::DataType::Pointer}, dyno::DataType::Void); });
	hookSay->addCallback(dyno::HookType::Pre, (dyno::HookHandler *)&Detour_Host_Say);

	return true;
}
