#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "File.h"
#include "../debug/Debug.h"

std::string File::readAllText(const char *filename)
{
	std::ifstream ifs(filename, std::ios::in | std::ios::binary);

	DebugAssertMsg(ifs.is_open(), "Could not open \"" + std::string(filename) + "\".");

	ifs.seekg(0, std::ios::end);
	std::string text(ifs.tellg(), '\0');
	ifs.seekg(0, std::ios::beg);
	
	ifs.read(&text[0], text.size());
	
	return text;
}

bool File::exists(const char *filename)
{
	std::error_code code;
	const bool success = std::filesystem::is_regular_file(filename, code);
	if (code)
	{
		DebugLogWarning("Couldn't check if file \"" + std::string(filename) + "\" exists: " + code.message());
		return false;
	}

	return success;
}

bool File::pathIsRelative(const char *filename)
{
	const size_t length = std::strlen(filename);
	DebugAssertMsg(length > 0, "Path cannot be empty.");

#if defined(_WIN32)
	// Can't be absolute without a colon at index 1.
	if (length < 2)
	{
		return true;
	}

	// Needs a drive letter and a colon to be absolute.
	return !(std::isalpha(static_cast<unsigned char>(filename[0])) && (filename[1] == ':'));
#else
	// Needs a leading forward slash to be absolute.
	return filename[0] != '/';
#endif
}

void File::copy(const char *srcFilename, const char *dstFilename)
{
	std::ifstream ifs(srcFilename, std::ios::binary);
	std::ofstream ofs(dstFilename, std::ios::binary);

	DebugAssertMsg(ifs.is_open(), "Cannot open \"" + std::string(srcFilename) + "\" for copying.");

	// Copy the source file to the destination.
	ofs << ifs.rdbuf();
}
