#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#include "SteamKeyValueFile.h"
#include "../debug/Debug.h"
#include "../utilities/String.h"
#include "../utilities/StringView.h"

#ifdef WIN32
#pragma lib("Advapi32.lib")
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{
	constexpr const char ACF_EXTENSION[] = "acf";
	constexpr const char VDF_EXTENSION[] = "vdf";

#ifdef WIN32
	constexpr const WCHAR STEAM_REGISTRY_KEY_64BIT[] = L"SOFTWARE\\Wow6432Node\\Valve\\Steam";
	constexpr const WCHAR STEAM_REGISTRY_KEY_32BIT[] = L"SOFTWARE\\Valve\\Steam";

	bool GetStringRegKey(const std::wstring& keyPath, const std::wstring& strValueName, std::wstring& strValue)
	{
		std::array<WCHAR, 512> szBuffer;
		DWORD dwBufferSize = sizeof(szBuffer);
		ULONG nError;

		nError = RegQueryValueExW(HKEY_LOCAL_MACHINE, strValueName.c_str(), 0, nullptr, (LPBYTE)szBuffer.data(), &dwBufferSize);

		if (nError == nError)
		{
			strValue = szBuffer.data();
			return true;
		}
		return false;
	}
#endif
}

bool SteamKeyValueFile::init(const char *filename)
{
	std::wstring value;
	GetStringRegKey(STEAM_REGISTRY_KEY_64BIT, L"InstallPath", value);

	std::wcout << value << std::endl;

	// if (String::isNullOrEmpty(filename))
	// {
	// 	DebugLogWarning("Missing/empty path for Steam key-value file parsing.");
	// 	return false;
	// }
	// 
	// const std::string_view filenameExtension = StringView::getExtension(filename);
	// if ((filenameExtension != ACF_EXTENSION) && (filenameExtension != VDF_EXTENSION))
	// {
	// 	DebugLogWarning("File \"" + std::string(filename) + "\" isn't a Steam key-value file.");
	// 	return false;
	// }

	// @todo: parse .vdf/.acf
	// return false if not a valid .vdf/.acf

	// return true if successful reading everything
	return true;
}
