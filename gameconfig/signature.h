#pragma once

#include <wchar.h>
#include <stdio.h>
#include <math.h>
#include <dlfcn.h>    /* dlsym */
#include <link.h>     /* link_map */
#include <unistd.h>   /* getpagesize */
#include <sys/mman.h> /* mprotect */
#include <sys/stat.h>
#include <cstring>
#include <link.h>
#include <stdio.h>
#include <stdexcept>
#include <fstream>
#include <iostream>

int hexStringToUint8Array(const char* hexString, uint8_t* byteArray, size_t maxBytes) {
    size_t hexStringLength = strlen(hexString);
    size_t byteCount = hexStringLength / 4; // Each "\\x" represents one byte.

    if (hexStringLength % 4 != 0 || byteCount == 0 || byteCount > maxBytes) {
        printf("Invalid hex string format or byte count.\n");
        return -1; // Return an error code.
    }

    for (size_t i = 0; i < hexStringLength; i += 4) {
        if (sscanf(hexString + i, "\\x%2hhX", &byteArray[i / 4]) != 1) {
            printf("Failed to parse hex string at position %zu.\n", i);
            return -1; // Return an error code.
        }
    }

    byteArray[byteCount] = '\0'; // Add a null-terminating character.

    return byteCount; // Return the number of bytes successfully converted.
}

long GetFileSize(std::string filename)
{
	struct stat stat_buf;
	int rc = stat(filename.c_str(), &stat_buf);
	return rc == 0 ? stat_buf.st_size : -1;
}

const char *GetFactoryModuleName(void* factory, char *conf_error, int conf_error_size)
{
	if (!factory)
	{
		snprintf(conf_error, conf_error_size, "Factory is not a valid pointer");
		return NULL;
	}

	Dl_info info;
	if (dladdr(factory, &info) != 0)
	{
        return info.dli_fname;
    }
    snprintf(conf_error, conf_error_size, "Failed to dladdr factory");
    return NULL;
}

void* find_sig(const char* module, const uint8_t* pattern, char *conf_error, int conf_error_size) {
    void *handle = dlopen(module, RTLD_LAZY|RTLD_NOW);
    if (!handle) {
		snprintf(conf_error, conf_error_size, "cannot open module %s (%s)", module, dlerror());
        return NULL;
    }
    struct link_map* lmap = (struct link_map*)handle;

	std::uintmax_t size = GetFileSize(lmap->l_name);
	std::ifstream moduleFile(lmap->l_name, std::ios_base::binary);
	if (!moduleFile)
	{
		snprintf(conf_error, conf_error_size, "Could not open module file %s", lmap->l_name);
		return NULL;
	}

	std::int8_t* data = new std::int8_t[size];
	moduleFile.read((char*)data, size);

	Elf64_Ehdr* elfHeader = (Elf64_Ehdr*)data;
	if (elfHeader->e_shoff == 0 || elfHeader->e_shstrndx == SHN_UNDEF)
	{
		snprintf(conf_error, conf_error_size, "Missing section header");
		delete[] data;
		return NULL;
	}

    uint8_t* start = (uint8_t*)lmap->l_addr;
    uint8_t* end   = start + size;

    dlclose(lmap);

    const uint8_t* memPos = start;
    const uint8_t* patPos = pattern;

    /* Iterate memory area until *patPos is '\0' (we found pattern).
     * If we start a pattern match, keep checking all pattern positions until we
     * are done or until mismatch. If we find mismatch, reset pattern position
     * and continue checking at the memory location where we started +1 */
    while (memPos < end && *patPos != '\0') {
        if (*memPos == *patPos || *patPos == '?') {
            memPos++;
            patPos++;
        } else {
            start++;
            memPos = start;
            patPos = pattern;
        }
    }

    /* We reached end of pattern, we found it */
    if (*patPos == '\0')
        return start;

    snprintf(conf_error, conf_error_size, "signature not found: <%s>", pattern);
    return NULL;
}
