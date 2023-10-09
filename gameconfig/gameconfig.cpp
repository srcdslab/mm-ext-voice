#include "gameconfig.h"
#include "signature.h"

CGameConfig::CGameConfig(const std::string& gameDir, const std::string& path)
{
	this->_gameDir = gameDir;
	this->_path = path;
	this->_kv = new KeyValues("Games");
}

bool CGameConfig::Init(IFileSystem *filesystem, char *conf_error, int conf_error_size)
{
	if (!_kv->LoadFromFile(filesystem, _path.c_str(), nullptr))
	{
		snprintf(conf_error, conf_error_size, "Failed to load gamedata file");
		return false;
	}

	const KeyValues* game = _kv->FindKey(_gameDir.c_str());
	if (game)
	{
#if defined _LINUX
		const char* platform = "linux";
#else
		const char* platform = "windows";
#endif

		const KeyValues* offsets = game->FindKey("Offsets");
		if (offsets)
		{
			FOR_EACH_SUBKEY(offsets, it)
			{
				_offsets[it->GetName()] = it->GetInt(platform, -1);
			}
		}

		const KeyValues* signatures = game->FindKey("Signatures");
		if (signatures)
		{
			FOR_EACH_SUBKEY(signatures, it)
			{
				const char* library = it->GetString("library");
				if (strcmp(library, "server") == 0 || strcmp(library, "engine") == 0)
				{
					_libraries[it->GetName()] = std::string(library);
				}
				else
				{
					snprintf(conf_error, conf_error_size, "Invalid library: %s", library);
					return false;
				}

				const char* signature = it->GetString(platform);
				_signatures[it->GetName()] = std::string(signature);

				// void* addr = nullptr;
				// if (signature[0])
				// {
				// 	if (signature[0] == '@')
				// 	{
				// 		addr = scanner->FindSymbol(&signature[1]);
				// 	}

				// 	if (addr == nullptr)
				// 	{
				// 		addr = scanner->FindSignature(signature);
				// 	}
				// }
				// _addresses[it->GetName()] = addr;
			}
		}
	}
	else
	{
		snprintf(conf_error, conf_error_size, "Failed to find game: %s", _gameDir.c_str());
		return false;
	}
	return true;
}

CGameConfig::~CGameConfig()
{
	delete _kv;
}

const std::string CGameConfig::GetPath()
{
	return _path;
}

std::string CGameConfig::GetSignature(const std::string& name)
{
	auto it = _signatures.find(name);
	if (it == _signatures.end())
	{
		return nullptr;
	}
	return it->second;
}

int CGameConfig::GetOffset(const std::string& name)
{
	auto it = _offsets.find(name);
	if (it == _offsets.end())
	{
		return -1;
	}
	return it->second;
}

std::string CGameConfig::GetLibrary(const std::string& name)
{
	auto it = _libraries.find(name);
	if (it == _libraries.end())
	{
		return nullptr;
	}
	return it->second;
}

void* CGameConfig::GetAddress(const std::string& name, void *engine, void *server, char *error, int maxlen)
{
	std::string library = this->GetLibrary(name);

	void *factory = NULL;
	if (strcmp(library.c_str(), "engine") == 0)
	{
		factory = (void*)engine;
	}
	else if (strcmp(library.c_str(), "server") == 0)
	{
		factory = (void*)server;
	}

	char conf_error[255] = "";
	const char *moduleName = GetFactoryModuleName(factory, conf_error, sizeof(conf_error));
	if (moduleName == NULL)
	{
		snprintf(error, maxlen, "Could not resolve factory module name: %s\n", conf_error);
		return NULL;
	}

	std::string hexSignature = this->GetSignature(name);
	size_t maxBytes = strlen(hexSignature.c_str()) / 4;
	uint8_t signature[maxBytes];
	int byteCount = hexStringToUint8Array(hexSignature.c_str(), signature, maxBytes);
	if (byteCount <= 0)
	{
		snprintf(error, maxlen, "Invalid signature format\n");
		return NULL;
	}

	return find_sig(moduleName, signature, conf_error, sizeof(conf_error));
}
