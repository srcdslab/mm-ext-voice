#pragma once

#include "KeyValues.h"

#include <string>
#include <unordered_map>

class CGameConfig
{
public:
	CGameConfig(const std::string& gameDir, const std::string& path);
	~CGameConfig();

	bool Init(IFileSystem *filesystem, char *conf_error, int conf_error_size);
	const std::string GetPath();
	std::string GetLibrary(const std::string& name);
	std::string GetSignature(const std::string& name);
	int GetOffset(const std::string& name);
	void* GetAddress(const std::string& name, void *engine, void *server, char *error, int maxlen);

private:
	std::string _gameDir;
	std::string _path;
	KeyValues* _kv;
	std::unordered_map<std::string, int> _offsets;
	std::unordered_map<std::string, std::string> _signatures;
	std::unordered_map<std::string, void*> _addresses;
	std::unordered_map<std::string, std::string> _libraries;
};
