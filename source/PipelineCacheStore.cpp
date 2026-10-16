#include "PipelineCacheStore.h"

#include <cstring>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <process.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace vkutil {

namespace {

// Layout of the Vulkan pipeline cache header (spec: "Pipeline Cache Header"):
//   uint32 headerLength
//   uint32 headerVersion
//   uint32 vendorID
//   uint32 deviceID
//   uint8  pipelineCacheUUID[VK_UUID_SIZE]
constexpr size_t kHeaderVersionOffset = sizeof(uint32_t);
constexpr size_t kVendorIDOffset = kHeaderVersionOffset + sizeof(uint32_t);
constexpr size_t kDeviceIDOffset = kVendorIDOffset + sizeof(uint32_t);
constexpr size_t kUUIDOffset = kDeviceIDOffset + sizeof(uint32_t);
constexpr size_t kHeaderSize = kUUIDOffset + VK_UUID_SIZE;

template <typename T>
auto readScalar(std::span<const std::byte> data, size_t offset) -> T {
  T value{};
  std::memcpy(&value, data.data() + offset, sizeof(T));
  return value;
}

auto currentProcessId() -> int64_t {
#if defined(_WIN32)
  return static_cast<int64_t>(_getpid());
#else
  return static_cast<int64_t>(::getpid());
#endif
}

// Flush a path to disk. Only the platforms that need it pay for it: the Windows
// CRT flushes on close, so these are no-ops there.
void syncFile(const fs::path &path) {
#if !defined(_WIN32)
  if (int fd = ::open(path.c_str(), O_WRONLY); fd >= 0) {
    ::fsync(fd);
    ::close(fd);
  }
#else
  (void)path;
#endif
}

void syncDirectory(const fs::path &path) {
#if !defined(_WIN32)
  if (int fd = ::open(path.c_str(), O_RDONLY); fd >= 0) {
    ::fsync(fd);
    ::close(fd);
  }
#else
  (void)path;
#endif
}

}  // namespace

auto parsePipelineCacheHeader(std::span<const std::byte> data)
    -> std::optional<PipelineCacheHeader> {
  if (data.size() < kHeaderSize) return std::nullopt;

  PipelineCacheHeader header;
  header.vendorID = readScalar<uint32_t>(data, kVendorIDOffset);
  header.deviceID = readScalar<uint32_t>(data, kDeviceIDOffset);
  std::memcpy(header.pipelineCacheUUID.data(), data.data() + kUUIDOffset,
              VK_UUID_SIZE);
  return header;
}

auto isPipelineCacheAcceptable(std::span<const std::byte> data,
                               const PipelineCacheIdentity &identity) -> bool {
  if (identity.pipelineCacheUUID == nullptr) return false;
  if (data.size() < kHeaderSize) return false;

  // auto: readScalar's explicit template argument already names the type, so
  // spelling it again here is the duplication modernize-use-auto flags.
  const auto headerLength = readScalar<uint32_t>(data, 0);
  const auto headerVersion = readScalar<uint32_t>(data, kHeaderVersionOffset);

  if (headerLength != kHeaderSize) return false;
  if (headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE) return false;

  const auto header = parsePipelineCacheHeader(data);
  if (!header.has_value()) return false;

  if (header->vendorID != identity.vendorID) return false;
  if (header->deviceID != identity.deviceID) return false;
  return std::memcmp(header->pipelineCacheUUID.data(),
                     identity.pipelineCacheUUID, VK_UUID_SIZE) == 0;
}

auto defaultCacheDir() -> std::optional<fs::path> {
#if defined(_WIN32)
  if (const char *localAppData = std::getenv("LOCALAPPDATA")) {
    return fs::path(localAppData);
  }
#elif defined(__APPLE__)
  if (const char *home = std::getenv("HOME")) {
    return fs::path(home) / "Library" / "Caches";
  }
#else
  if (const char *xdg = std::getenv("XDG_CACHE_HOME"); xdg && xdg[0] != '\0') {
    return fs::path(xdg);
  }
  if (const char *home = std::getenv("HOME")) {
    return fs::path(home) / ".cache";
  }
#endif
  return std::nullopt;
}

auto pipelineCachePathIn(const fs::path &dir,
                         const std::string &appName) -> fs::path {
  return dir / appName / "pipeline_cache.bin";
}

auto defaultPipelineCachePath(const std::string &appName) -> fs::path {
  if (const auto dir = defaultCacheDir()) {
    return pipelineCachePathIn(*dir, appName);
  }
  // No platform cache location: keep the old behaviour of a CWD-relative file
  // rather than failing to persist anything.
  return {"pipeline_cache.bin"};
}

void sweepStaleTemps(const fs::path &target, std::chrono::seconds staleAfter) {
  std::error_code ec;
  const fs::path dir = target.parent_path();
  if (dir.empty()) return;
  const std::string prefix = target.filename().string() + ".tmp.";

  const auto now = fs::file_time_type::clock::now();

  for (fs::directory_iterator it(dir, ec), end; !ec && it != end;
       it.increment(ec)) {
    const std::string filename = it->path().filename().string();
    if (!filename.starts_with(prefix)) continue;  // not one of our temps

    const auto mtime = fs::last_write_time(it->path(), ec);
    if (ec) {
      ec.clear();
      continue;
    }
    if (now - mtime < staleAfter) continue;  // recent — possibly a live writer

    fs::remove(it->path(), ec);
    ec.clear();
  }
}

auto writeFileAtomically(const fs::path &path,
                         std::span<const std::byte> data) -> bool {
  std::error_code ec;
  // Only when there is a parent to create: a bare relative filename (the
  // no-platform-cache-dir fallback in defaultPipelineCachePath) has an empty
  // parent_path(), and create_directories("") sets EINVAL. The error_code
  // overload does not throw, so this is not a correctness fix — the file is
  // still written — it just avoids recording a meaningless error in `ec`,
  // which is checked below for the rename. Errors are otherwise ignored:
  // opening the file fails gracefully and persistence is best-effort.
  if (!path.parent_path().empty()) {
    fs::create_directories(path.parent_path(), ec);
  }

  // PID-suffix the temp name so two concurrent processes don't clobber each
  // other's in-progress writes.
  fs::path tmpPath = path;
  tmpPath += ".tmp." + std::to_string(currentProcessId());

  {
    std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;
    file.write(reinterpret_cast<const char *>(data.data()),
               static_cast<std::streamsize>(data.size()));
    if (!file.good()) {
      file.close();
      fs::remove(tmpPath, ec);
      return false;
    }
  }

  // Sync data to disk before the rename so a power loss between rename and the
  // kernel flushing dirty pages can't leave a torn cache.
  syncFile(tmpPath);

  fs::rename(tmpPath, path, ec);
  if (ec) {
    fs::remove(tmpPath, ec);
    return false;
  }

  // fsync the parent directory so the rename itself is durable.
  if (!path.parent_path().empty()) {
    syncDirectory(path.parent_path());
  }
  return true;
}

auto readFileFully(const fs::path &path)
    -> std::optional<std::vector<std::byte>> {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return std::nullopt;

  const auto size = file.tellg();
  if (size <= 0) return std::nullopt;

  std::vector<std::byte> data(static_cast<size_t>(size));
  file.seekg(0);
  file.read(reinterpret_cast<char *>(data.data()), size);
  if (!file.good()) return std::nullopt;

  return data;
}

PipelineCacheStore::PipelineCacheStore(fs::path cacheFile)
    : cacheFile(std::move(cacheFile)) {}

auto PipelineCacheStore::load(const PipelineCacheIdentity &identity) const
    -> std::optional<std::vector<std::byte>> {
  // Clear out any temp files left by a process killed mid-save.
  sweepStaleTemps(cacheFile);

  auto data = readFileFully(cacheFile);
  if (!data.has_value()) return std::nullopt;

  // A blob built for another device or driver is not valid initial data, and
  // passing it to vkCreatePipelineCache would fail (or be silently ignored).
  if (!isPipelineCacheAcceptable(*data, identity)) return std::nullopt;

  return data;
}

auto PipelineCacheStore::save(std::span<const std::byte> data) const -> bool {
  if (data.empty()) return false;
  return writeFileAtomically(cacheFile, data);
}

}  // namespace vkutil
