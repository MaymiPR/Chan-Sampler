// The macOS clipboard (SampleExchangeMac.mm), called from SampleExchange.cpp.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace exchange
{
bool macCopyToClipboard (const char* path, const void* wav, size_t size);
bool macClipboardHasAudio();
bool macPasteFromClipboard (std::string& path, std::vector<char>& data, std::string& extension);
}
