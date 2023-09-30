#ifndef _INCLUDE_METAMOD_UTILS_VOICE_H_
#define _INCLUDE_METAMOD_UTILS_VOICE_H_

#include <string>
#include <iostream>

std::string string_to_hex(const std::string& input);
int hex_value(unsigned char hex_digit);
std::string hex_to_string(const std::string& input);
std::string getDirectoryName(const std::string& directoryPathInput);
double getTime();

#endif //_INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
