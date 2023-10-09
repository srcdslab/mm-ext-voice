#ifndef _INCLUDE_METAMOD_SOURCE_VOICE_H_
#define _INCLUDE_METAMOD_SOURCE_VOICE_H_

// Base
#include <poll.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

// Celt
#include "celt/celt_header.h"

// Extension
#include "ringbuffer.h"
#include "detour/functions.h"

// HL2SDK
#include <ISmmPlugin.h>
#include <igameevents.h>
#include <iplayerinfo.h>
#include <sh_vector.h>
#include "bitbuf.h"
// #include "iserver.h"

class IClient;

extern ISmmAPI *g_SMAPI;
extern ISmmPlugin *g_PLAPI;
extern CGlobalVars *gpGlobals;

#define MAX_CLIENTS 16
#define MAX_PLAYERS 65

// voice packets are sent over unreliable netchannel
// #define NET_MAX_DATAGRAM_PAYLOAD	4000	// = maximum unreliable payload size
// voice packetsize = 64 | netchannel overflows at >4000 bytes
// 2009 Games with 22050 samplerate and 512 frames per packet -> 23.22ms per packet
// Newer games with 44100 samplerate and 512 frames per packet -> 11.60ms per packet
// 2009 Games SVC_VoiceData overhead = 5 bytes
// 2009 Games sensible limit of 8 packets per frame = 552 bytes -> 185.76ms of voice data per frame
// Newer games sensible limit of 8 packets per frame = 552 bytes -> 82.80ms of voice data per frame
#define NET_MAX_VOICE_BYTES_FRAME (8 * (5 + 64))

template <typename T>
inline T min(T a, T b) { return a < b ? a : b; }

class SvLogging
{
public:
	SvLogging(){};
	~SvLogging(){};

	int GetInt()
	{
		return 1;
	}
};

class SvTestDataHex
{
public:
	SvTestDataHex(){};
	~SvTestDataHex(){};

	const char *GetString()
	{
		return "";
	}
};

class SvCallOriginalBroadcast
{
public:
	SvCallOriginalBroadcast(){};
	~SvCallOriginalBroadcast(){};

	int GetInt()
	{
		return 1;
	}
};

class SvVoiceAddr
{
public:
	SvVoiceAddr(){};
	~SvVoiceAddr(){};

	const char *GetString()
	{
		return "127.0.0.1";
	}
};

class SvVoicePort
{
public:
	SvVoicePort(){};
	~SvVoicePort(){};

	int GetInt()
	{
		return 27030;
	}
};

class ThreadClientSpeaking
{
public:
	ThreadClientSpeaking() : stopFlag(false), threadRunning(false) {}

	void start();
	void stop();

private:
	std::atomic<bool> stopFlag;
	std::atomic<bool> threadRunning;
	std::thread myThread;

	void loopFunction();
};

class Voice
{
public:
	Voice();
	~Voice();
	bool Init(char *error, size_t maxlen);
	bool OnAllPluginsLoaded(char *error, size_t maxlen);
	void OnGameFrame(bool simulating);
	bool OnBroadcastVoiceData(IClient *pClient, int nBytes, char *data);
	bool ListenSocket(char *error, size_t maxlen);

	bool IsClientSpeaking(int client);
	void OnClientSpeakingStart(int client);
	void OnClientSpeaking(int client);
	void OnClientSpeakingEnd(int client);

private:
	int m_ListenSocket;

	struct CClient
	{
		int m_Socket;
		size_t m_BufferWriteIndex;
		size_t m_LastLength;
		double m_LastValidData;
		bool m_New;
		bool m_UnEven;
		unsigned char m_Remainder;
	} m_aClients[MAX_CLIENTS];

	struct pollfd m_aPollFds[1 + MAX_CLIENTS];
	int m_PollFds;

	CRingBuffer m_Buffer;

	double m_AvailableTime;

	struct CEncoderSettings
	{
		celt_int32 SampleRate_Hz;
		celt_int32 TargetBitRate_Kbps;
		int FrameSize;
		celt_int32 PacketSize;
		celt_int32 Complexity;
		double FrameTime;
	} m_EncoderSettings;

	CELTMode *m_pMode;
	CELTEncoder *m_pCodec;

	void HandleNetwork();
	void OnDataReceived(CClient *pClient, int16_t *pData, size_t Samples);
	void HandleVoiceData();
	void BroadcastVoiceData(INetworkGameServer *pServer, IClient *pClient, size_t nBytes, unsigned char *pData);
};

#endif //_INCLUDE_METAMOD_SOURCE_VOICE_H_
