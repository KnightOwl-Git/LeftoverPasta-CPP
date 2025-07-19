#include "asset_utils.hpp"
#include <filesystem>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/dyld.h>
#include <climits>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#include <climits>
#endif

std::string getAssetPath(const std::string &filename) {
#if defined(__APPLE__)
  CFBundleRef mainBundle = CFBundleGetMainBundle();
  CFStringRef cfFilename = CFStringCreateWithCString(NULL, filename.c_str(), kCFStringEncodingUTF8);
  CFURLRef resourceURL = CFBundleCopyResourceURL(mainBundle, cfFilename, NULL, NULL);

  char path[PATH_MAX];
  if (resourceURL && CFURLGetFileSystemRepresentation(resourceURL, true, (UInt8 *)path, sizeof(path))) {
    CFRelease(resourceURL);
    CFRelease(cfFilename);
    return std::string(path);
  }

  if (resourceURL) {
    CFRelease(resourceURL);
  }
  if (cfFilename) {
    CFRelease(cfFilename);
  }

#endif

  //other platforms
  std::filesystem::path exePath;
#if defined(_WIN32)
  char buffer[MAX_PATH];
  GetModuleFileName(NULL, buffer, MAX_PATH);
  exePath = std::filesystem::path(buffer);
#elif defined(__linux__)
  char buffer[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (len > 0) {
    buffer[len] = '\0';
    exePath = buffer;
  }
#elif defined(__APPLE__)
  // apple fallback for if there's no bundle
  char buffer[PATH_MAX];
  uint32_t size = sizeof(buffer);
  if (_NSGetExecutablePath(buffer, &size) == 0) {
    exePath = buffer;
  }
#endif
  auto exeDir = exePath.parent_path();
  auto assetPath = exeDir / "assets" / filename;
  return assetPath.string();
}
