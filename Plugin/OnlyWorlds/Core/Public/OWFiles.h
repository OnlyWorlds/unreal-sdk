#pragma once
// The few file operations the core needs, on std::filesystem. Paths are UTF-8 strings, '/' or '\' separated; names
// come back as UTF-8. This is the seam the Unreal module may later replace with IFileManager.

#include <cstdint>
#include <string>
#include <vector>

namespace OnlyWorlds {

using OWBytes = std::vector<uint8_t>;

namespace OWFiles {

bool Read(const std::string& path, OWBytes& out);
// Write the bytes, making the parent folders first (unless make_dirs is false: the caller made them).
bool Write(const std::string& path, const OWBytes& data, bool make_dirs = true);
// Make a folder and its parents; true when it exists after.
bool MakeDirs(const std::string& dir);
bool IsFile(const std::string& path);
bool IsDir(const std::string& path);
// The names of the files (dirs false) or folders (dirs true) in a folder, sorted by their UTF-8 bytes. Dot-names are
// listed too: the caller decides what a dot means.
std::vector<std::string> List(const std::string& dir, bool dirs);
// Remove a folder and everything in it (a scratch folder); true when nothing is left.
bool RemoveTree(const std::string& dir);
// The path as an absolute, normalised one with '/' separators.
std::string Absolute(const std::string& path);
// The last segment of a path ("abbey" for "C:/worlds/abbey/").
std::string LastSegment(const std::string& path);

}  // namespace OWFiles
}  // namespace OnlyWorlds
