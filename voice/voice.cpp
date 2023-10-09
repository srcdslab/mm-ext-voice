#include "voice.h"
#include "utils/utils.h"
#include "detour/functions.h"

// Protobuf
#include "cs2/netmessages.pb.h"

#include <stdexcept>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>

#include <ihltvdirector.h>
#include <ihltv.h>

IHLTVDirector *hltvdirector = NULL;
IHLTVServer *hltv = NULL;

ThreadClientSpeaking theadClientSpeaking;

extern SV_BroadcastVoiceData g_pSV_BroadcastVoiceData;

SvLogging *g_SvLogging = new SvLogging();
SvTestDataHex *g_SvTestDataHex = new SvTestDataHex();
SvCallOriginalBroadcast *g_SvCallOriginalBroadcast = new SvCallOriginalBroadcast();
SvVoiceAddr *g_SmVoiceAddr = new SvVoiceAddr();
SvVoicePort *g_SmVoicePort = new SvVoicePort();

int g_aFrameVoiceBytes[MAX_PLAYERS + 1] = {0};
double g_fLastVoiceData[MAX_PLAYERS + 1] = {0};
bool g_bClientSpeaking[MAX_PLAYERS + 1] = {false};

bool g_startGameFrame = false;

extern Voice *g_Voice;

void ThreadClientSpeaking::start()
{
	if (!threadRunning)
	{
		threadRunning = true;
		myThread = std::thread(&ThreadClientSpeaking::loopFunction, this);
	}
}

void ThreadClientSpeaking::stop()
{
	{
		if (threadRunning)
		{
			stopFlag = true;
			myThread.join();
			threadRunning = false;
		}
	}
}

void ThreadClientSpeaking::loopFunction()
{
	while (!stopFlag)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(300)); // Sleep for 300 milliseconds

		// Wait till globals are loaded
		if (gpGlobals == NULL)
			continue;

		for (int client = 0; client < MAX_PLAYERS; client++)
		{
			if ((gpGlobals->curtime - g_fLastVoiceData[client]) > 0.1f)
			{
				if (g_bClientSpeaking[client])
				{
					g_bClientSpeaking[client] = false;
					g_Voice->OnClientSpeakingEnd(client);
				}
			}
			else
			{
				if (!g_bClientSpeaking[client])
				{
					g_bClientSpeaking[client] = true;
					g_Voice->OnClientSpeakingStart(client);
				}
				else
				{
					g_Voice->OnClientSpeaking(client);
				}
			}
		}
	}
}

Voice::Voice()
{
	theadClientSpeaking.start();

	m_ListenSocket = -1;

	m_PollFds = 0;
	for (int i = 1; i < 1 + MAX_CLIENTS; i++)
		m_aPollFds[i].fd = -1;

	for (int i = 0; i < MAX_CLIENTS; i++)
		m_aClients[i].m_Socket = -1;

	m_AvailableTime = 0.0;

	m_pMode = NULL;
	m_pCodec = NULL;
}

Voice::~Voice()
{
	theadClientSpeaking.stop();
}

bool Voice::Init(char *error, size_t maxlen)
{
	// Encoder settings
	m_EncoderSettings.SampleRate_Hz = 24000;
	m_EncoderSettings.TargetBitRate_Kbps = 64;
	m_EncoderSettings.FrameSize = 512; // samples
	m_EncoderSettings.PacketSize = 64;
	m_EncoderSettings.Complexity = 10; // 0 - 10
	m_EncoderSettings.FrameTime = (double)m_EncoderSettings.FrameSize / (double)m_EncoderSettings.SampleRate_Hz;

	if (g_SvLogging->GetInt())
	{
		META_CONPRINTF("[VOICE] == Voice Encoder Settings ==\n");
		META_CONPRINTF("[VOICE] SampleRateHertzKbps: %d\n", m_EncoderSettings.SampleRate_Hz);
		META_CONPRINTF("[VOICE] BitRate: %d\n", m_EncoderSettings.TargetBitRate_Kbps);
		META_CONPRINTF("[VOICE] FrameSize: %d\n", m_EncoderSettings.FrameSize);
		META_CONPRINTF("[VOICE] PacketSize: %d\n", m_EncoderSettings.PacketSize);
		META_CONPRINTF("[VOICE] Complexity: %d\n", m_EncoderSettings.Complexity);
		META_CONPRINTF("[VOICE] FrameTime: %lf\n", m_EncoderSettings.FrameTime);
	}

	// Init CELT encoder
	// int theError;
	// m_pMode = celt_mode_create(m_EncoderSettings.SampleRate_Hz, m_EncoderSettings.FrameSize, &theError);
	// if (m_pMode == NULL)
	// {
	// 	g_SMAPI->Format(error, maxlen, "celt_mode_create error: %d", theError);
	// 	return false;
	// }

	// m_pCodec = celt_encoder_create_custom(m_pMode, 1, &theError);
	// if (!m_pCodec)
	// {
	// 	g_SMAPI->Format(error, maxlen, "celt_encoder_create_custom error: %d", theError);
	// 	return false;
	// }

	// celt_encoder_ctl(m_pCodec, CELT_RESET_STATE_REQUEST, NULL);
	// celt_encoder_ctl(m_pCodec, CELT_SET_BITRATE(m_EncoderSettings.TargetBitRate_Kbps * 1000));
	// celt_encoder_ctl(m_pCodec, CELT_SET_COMPLEXITY(m_EncoderSettings.Complexity));
	return true;
}

bool Voice::OnAllPluginsLoaded(char *error, size_t maxlen)
{
	// Init tcp server
	m_ListenSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (m_ListenSocket < 0)
	{
		g_SMAPI->Format(error, maxlen, "Failed creating socket.");
		return false;
	}

	int yes = 1;
	if (setsockopt(m_ListenSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0)
	{
		g_SMAPI->Format(error, maxlen, "Failed setting SO_REUSEADDR on socket.");
		return false;
	}

	return this->ListenSocket(error, maxlen);
}

bool Voice::ListenSocket(char *error, size_t maxlen)
{
	if (m_PollFds > 0)
	{
		g_SMAPI->Format(error, maxlen, "Invalid poll fds");
		return false;
	}

	sockaddr_in bindAddr;
	memset(&bindAddr, 0, sizeof(bindAddr));
	bindAddr.sin_family = AF_INET;
	inet_aton(g_SmVoiceAddr->GetString(), &bindAddr.sin_addr);
	bindAddr.sin_port = htons(g_SmVoicePort->GetInt());

	META_CONPRINTF("[VOICE] Binding to %s:%d!\n", g_SmVoiceAddr->GetString(), g_SmVoicePort->GetInt());

	if (bind(m_ListenSocket, (sockaddr *)&bindAddr, sizeof(sockaddr_in)) < 0)
	{
		g_SMAPI->Format(error, maxlen, "Failed binding to socket (%d '%s').", errno, strerror(errno));
		return false;
	}

	if (listen(m_ListenSocket, MAX_CLIENTS) < 0)
	{
		g_SMAPI->Format(error, maxlen, "Failed listening on socket.");
		return false;
	}

	m_aPollFds[0].fd = m_ListenSocket;
	m_aPollFds[0].events = POLLIN;
	m_PollFds++;

	g_startGameFrame = true;
	return true;
}

void Voice::OnGameFrame(bool simulating)
{
	if (!g_startGameFrame)
		return;

	HandleNetwork();
	HandleVoiceData();

	// Reset per-client voice byte counter to 0 every frame.
	memset(g_aFrameVoiceBytes, 0, sizeof(g_aFrameVoiceBytes));
}

void Voice::HandleNetwork()
{
	if (m_ListenSocket == -1)
		return;

	int pollRes = poll(m_aPollFds, m_PollFds, 0);
	if (pollRes <= 0)
		return;

	// Accept new clients
	if (m_aPollFds[0].revents & POLLIN)
	{
		// Find slot
		int client = 0;
		while (client < MAX_CLIENTS)
		{
			if (m_aClients[client].m_Socket == -1)
				break;

			client++;
		}

		// no free slot
		if (client != MAX_CLIENTS)
		{
			sockaddr_in addr;
			socklen_t size = sizeof(addr);
			int socket = accept(m_ListenSocket, (sockaddr *)&addr, &size);

			m_aClients[client].m_Socket = socket;
			m_aClients[client].m_BufferWriteIndex = 0;
			m_aClients[client].m_LastLength = 0;
			m_aClients[client].m_LastValidData = 0.0;
			m_aClients[client].m_New = true;
			m_aClients[client].m_UnEven = false;

			m_aPollFds[m_PollFds].fd = socket;
			m_aPollFds[m_PollFds].events = POLLIN | POLLHUP;
			m_aPollFds[m_PollFds].revents = 0;
			m_PollFds++;

			if (g_SvLogging->GetInt())
				META_CONPRINTF("[VOICE] Client %d connected!\n", client);
		}
	}

	bool compressPollFds = false;
	for (int pollFds = 1; pollFds < m_PollFds; pollFds++)
	{
		int client = 0;
		while (client < MAX_CLIENTS)
		{
			if (m_aClients[client].m_Socket == m_aPollFds[pollFds].fd)
				break;
			client++;
		}
		if (client >= MAX_CLIENTS)
			continue;

		CClient *pClient = &m_aClients[client];

		// Connection shutdown prematurely ^C
		// Make sure to set SO_LINGER l_onoff = 1, l_linger = 0
		if (m_aPollFds[pollFds].revents & POLLHUP)
		{
			if (pClient->m_Socket != -1)
				close(pClient->m_Socket);

			pClient->m_Socket = -1;
			m_aPollFds[pollFds].fd = -1;
			compressPollFds = true;
			if (g_SvLogging->GetInt())
				META_CONPRINTF("[VOICE] Client %d disconnected!(2)\n", client);
			continue;
		}

		// Data available?
		if (!(m_aPollFds[pollFds].revents & POLLIN))
			continue;

		size_t BytesAvailable;
		if (ioctl(pClient->m_Socket, FIONREAD, &BytesAvailable) == -1)
			continue;

		if (pClient->m_New)
		{
			pClient->m_BufferWriteIndex = m_Buffer.GetReadIndex();
			pClient->m_New = false;
		}

		m_Buffer.SetWriteIndex(pClient->m_BufferWriteIndex);

		// Don't recv() when we can't fit data into the ringbuffer
		unsigned char aBuf[32768];
		if (min(BytesAvailable, sizeof(aBuf)) > m_Buffer.CurrentFree() * sizeof(int16_t))
			continue;

		// Edge case: previously received data is uneven and last recv'd byte has to be prepended
		int Shift = 0;
		if (pClient->m_UnEven)
		{
			Shift = 1;
			aBuf[0] = pClient->m_Remainder;
			pClient->m_UnEven = false;
		}

		ssize_t Bytes = recv(pClient->m_Socket, &aBuf[Shift], sizeof(aBuf) - Shift, 0);

		if (Bytes <= 0)
		{
			if (pClient->m_Socket != -1)
				close(pClient->m_Socket);

			pClient->m_Socket = -1;
			m_aPollFds[pollFds].fd = -1;
			compressPollFds = true;
			if (g_SvLogging->GetInt())
				META_CONPRINTF("[VOICE] Client %d disconnected!(1)\n", client);
			continue;
		}

		Bytes += Shift;

		// Edge case: data received is uneven (can't be divided by two)
		// store last byte, drop it here and prepend it right before the next recv
		if (Bytes & 1)
		{
			pClient->m_UnEven = true;
			pClient->m_Remainder = aBuf[Bytes - 1];
			Bytes -= 1;
		}

		// Got data!
		OnDataReceived(pClient, (int16_t *)aBuf, Bytes / sizeof(int16_t));

		pClient->m_LastLength = m_Buffer.CurrentLength();
		pClient->m_BufferWriteIndex = m_Buffer.GetWriteIndex();
	}

	if (compressPollFds)
	{
		for (int pollFds = 1; pollFds < m_PollFds; pollFds++)
		{
			if (m_aPollFds[pollFds].fd != -1)
				continue;

			for (int pollFds_ = pollFds; pollFds_ < 1 + MAX_CLIENTS; pollFds_++)
				m_aPollFds[pollFds_].fd = m_aPollFds[pollFds_ + 1].fd;

			pollFds--;
			m_PollFds--;
		}
	}
}

void Voice::OnDataReceived(CClient *pClient, int16_t *pData, size_t Samples)
{
	// Check for empty input
	ssize_t DataStartsAt = -1;
	for (size_t i = 0; i < Samples; i++)
	{
		if (pData[i] == 0)
			continue;

		DataStartsAt = i;
		break;
	}

	// Discard empty data if last vaild data was more than a second ago.
	if (pClient->m_LastValidData + 1.0 < getTime())
	{
		// All empty
		if (DataStartsAt == -1)
			return;

		// Data starts here
		pData += DataStartsAt;
		Samples -= DataStartsAt;
	}

	if (!m_Buffer.Push(pData, Samples))
	{
		META_CONPRINTF("[VOICE] [ERROR] Buffer push failed!!! Samples: %zu, Free: %zu\n", Samples, m_Buffer.CurrentFree());
		return;
	}

	pClient->m_LastValidData = getTime();
}

void Voice::HandleVoiceData()
{
	int SamplesPerFrame = m_EncoderSettings.FrameSize;
	int PacketSize = m_EncoderSettings.PacketSize;
	int FramesAvailable = m_Buffer.TotalLength() / SamplesPerFrame;
	float TimeAvailable = (float)m_Buffer.TotalLength() / (float)m_EncoderSettings.SampleRate_Hz;

	if (!FramesAvailable)
		return;

	// Before starting playback we want at least 100ms in the buffer
	if (m_AvailableTime < getTime() && TimeAvailable < 0.1)
		return;

	// let the clients have no more than 500ms
	if (m_AvailableTime > getTime() + 0.5)
		return;

	// 5 = max frames per packet
	FramesAvailable = min(FramesAvailable, 5);

	// Get SourceTV Index
	if (!hltv)
	{
		hltv = hltvdirector->GetHLTVServer();
	}

	int iSourceTVIndex = 0;
	if (hltv)
		iSourceTVIndex = hltv->GetHLTVSlot();

	// TODO: Get HLTV slot
	INetworkGameServer *pServer = NULL;
	// INetworkGameServer *pServer = hltv->GetBaseServer();

	IClient *pClient = NULL;
	// IClient *pClient = pServer->GetClient(iSourceTVIndex);
	// if(!pClient)
	// {
	// 	META_CONPRINTF("[VOICE] [ERROR] Couldnt get client with id %d (SourceTV)\n", iSourceTVIndex);
	// 	return;
	// }

	for (int Frame = 0; Frame < FramesAvailable; Frame++)
	{
		// Get data into buffer from ringbuffer.
		int16_t aBuffer[SamplesPerFrame];

		size_t OldReadIdx = m_Buffer.m_ReadIndex;
		size_t OldCurLength = m_Buffer.CurrentLength();
		size_t OldTotalLength = m_Buffer.TotalLength();

		if (!m_Buffer.Pop(aBuffer, SamplesPerFrame))
		{
			printf("Buffer pop failed!!! Samples: %d, Length: %zu\n", SamplesPerFrame, m_Buffer.TotalLength());
			return;
		}

		// Encode it!
		unsigned char aFinal[PacketSize];
		int FinalSize = 0;

		// FinalSize = celt_encode(m_pCodec, aBuffer, SamplesPerFrame, aFinal, sizeof(aFinal));

		if (FinalSize <= 0)
		{
			META_CONPRINTF("[VOICE] [ERROR] Compress returned %d\n", FinalSize);
			return;
		}

		// Check for buffer underruns
		for (int client = 0; client < MAX_CLIENTS; client++)
		{
			CClient *pCClient = &m_aClients[client];
			if (pCClient->m_Socket == -1 || pCClient->m_New == true)
				continue;

			m_Buffer.SetWriteIndex(pCClient->m_BufferWriteIndex);

			if (m_Buffer.CurrentLength() > pCClient->m_LastLength)
			{
				pCClient->m_BufferWriteIndex = m_Buffer.GetReadIndex();
				m_Buffer.SetWriteIndex(pCClient->m_BufferWriteIndex);
				pCClient->m_LastLength = m_Buffer.CurrentLength();
			}
		}

		BroadcastVoiceData(pServer, pClient, FinalSize, aFinal);
	}

	if (m_AvailableTime < getTime())
		m_AvailableTime = getTime();

	m_AvailableTime += (double)FramesAvailable * m_EncoderSettings.FrameTime;
}

bool Voice::OnBroadcastVoiceData(IClient *pClient, int nBytes, char *data)
{
	// TODO: get real player slot
	int client = 0 + 1;
	// int client = pClient->GetPlayerSlot() + 1;

	META_CONPRINTF("[VOICE] Curtime: %f\n", gpGlobals->curtime);
	g_fLastVoiceData[client] = gpGlobals->curtime;

	// Reject empty packets
	if (nBytes < 1)
		return false;

	// Reject voice packet if we'd send more than NET_MAX_VOICE_BYTES_FRAME voice bytes from this client in the current frame.
	// 5 = SVC_VoiceData header/overhead
	g_aFrameVoiceBytes[client] += 5 + nBytes;

	return true;
}

void Voice::BroadcastVoiceData(INetworkGameServer *pServer, IClient *pClient, size_t nBytes, unsigned char *pData)
{
	if (!this->OnBroadcastVoiceData(pClient, nBytes, (char *)pData))
		return;

	bool drop = false; // if (!pDestClient->IsSplitScreenUser() && (!drop || !IsReplay/IsHLTV())
	static ::google::protobuf::int32 sequence_bytes = 0;
	static ::google::protobuf::uint32 section_number = 0;
	static ::google::protobuf::uint32 uncompressed_sample_offset = 0;

	// TODO: get real player slot
	int client = 0 + 1;
	// int client = pClient->GetPlayerSlot() + 1;

	if (!g_bClientSpeaking[client])
	{
		section_number++;
		sequence_bytes = 0;
		uncompressed_sample_offset = 0;
	}

	CMsgVoiceAudio *audio = new CMsgVoiceAudio();
	// SteamID3
	uint64_t xuid = 0;

	if (strcmp(g_SvTestDataHex->GetString(), "") == 0)
	{
		sequence_bytes += nBytes;
		audio->set_voice_data((char *)pData, nBytes);
	}
	else
	{
		::std::string testing = hex_to_string(g_SvTestDataHex->GetString());
		sequence_bytes += nBytes;
		audio->set_voice_data(testing.c_str(), testing.size());
	}

	uncompressed_sample_offset += m_EncoderSettings.FrameSize;

	audio->set_format(VOICEDATA_FORMAT_ENGINE);
	audio->set_sequence_bytes(sequence_bytes);

	// These two values set to 0 will make it them ignored
	audio->set_section_number(0);
	audio->set_uncompressed_sample_offset(0);

	if (g_SvLogging->GetInt())
		PrintCMsgVoiceAudio(audio);

	if (g_SvCallOriginalBroadcast->GetInt())
		g_pSV_BroadcastVoiceData(pServer, pClient, audio, xuid);
}

bool Voice::IsClientSpeaking(int client)
{
	if (client < 0 || client >= MAX_PLAYERS)
		return false;

	return g_bClientSpeaking[client];
}

void Voice::OnClientSpeakingStart(int client)
{
	if (g_SvLogging->GetInt())
		META_CONPRINTF("[VOICE] OnClientSpeakingStart (client=%d)\n", client);
}

void Voice::OnClientSpeaking(int client)
{
	if (g_SvLogging->GetInt())
		META_CONPRINTF("[VOICE] OnClientSpeaking (client=%d)\n", client);
}

void Voice::OnClientSpeakingEnd(int client)
{
	if (g_SvLogging->GetInt())
		META_CONPRINTF("[VOICE] OnClientSpeakingEnd (client=%d)\n", client);
}
