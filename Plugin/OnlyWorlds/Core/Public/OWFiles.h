#pragma once
// The few file operations the core needs, on std::filesystem. Paths are UTF-8 strings, '/' or '\' separated; names
// come back as UTF-8. This is the seam the Unreal module may later replace with IFileManager.

#include "OWApi.h"

#include <cstdint>
#include <string>
#include <vector>

namespace OnlyWorlds {

using OWBytes = std::vector<uint8_t>;

namespace OWFiles {

OW_API bool Read(const std::string& path, OWBytes& out);
// Write the bytes, making the parent folders first (unless make_dirs is false: the caller made them).
OW_API bool Write(const std::string& path, const OWBytes& data, bool make_dirs = true);
// Make a folder and its parents; true when it exists after.
OW_API bool MakeDirs(const std::string& dir);
OW_API bool IsFile(const std::string& path);
OW_API bool IsDir(const std::string& path);
// The names of the files (dirs false) or folders (dirs true) in a folder, sorted by their UTF-8 bytes. Dot-names are
// listed too: the caller decides what a dot means.
OW_API std::vector<std::string> List(const std::string& dir, bool dirs);
// Remove a folder and everything in it (a scratch folder); true when nothing is left.
OW_API bool RemoveTree(const std::string& dir);
// Move (rename) a file or a folder to a path that is not there; true when it moved. On one volume only.
OW_API bool Move(const std::string& from, const std::string& to);
// The path as an absolute, normalised one with '/' separators.
OW_API std::string Absolute(const std::string& path);
// The last segment of a path ("abbey" for "C:/worlds/abbey/").
OW_API std::string LastSegment(const std::string& path);

}  // namespace OWFiles
}  // namespace OnlyWorlds
