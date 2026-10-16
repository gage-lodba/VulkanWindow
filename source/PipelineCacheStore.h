#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// On-disk persistence for the Vulkan pipeline cache.
///
/// Split out of VulkanContext because none of it needs a Vulkan device: path
/// derivation, the cache-header validation, and the crash-safe write are all
/// plain data-in/data-out or filesystem work, so they can be unit-tested
/// without a GPU, a window, or a display. VulkanContext keeps only the two
/// vkGetPipelineCacheData / vkCreatePipelineCache calls that bracket this.
namespace vkutil {

/// The device/driver fingerprint a pipeline-cache blob is valid for. Mirrors
/// the fields Vulkan specifies for the cache header (see the spec's
/// "Pipeline Cache Header" section). A blob whose header disagrees with the
/// current device must not be passed to vkCreatePipelineCache — a cache built
/// for a different device or driver is invalid data.
struct PipelineCacheIdentity {
  uint32_t vendorID{0};
  uint32_t deviceID{0};
  /// VK_UUID_SIZE bytes from VkPhysicalDeviceProperties::pipelineCacheUUID.
  const uint8_t *pipelineCacheUUID{nullptr};
};

/// The first VK_UUID_SIZE bytes of a cache blob that identify the device it was
/// built for. std::array rather than a C array so the size travels with the
/// type (and so modernize-avoid-c-arrays stays quiet).
struct PipelineCacheHeader {
  uint32_t vendorID{0};
  uint32_t deviceID{0};
  std::array<uint8_t, VK_UUID_SIZE> pipelineCacheUUID{};
};

/// Parse the device-identifying header out of a raw pipeline-cache blob.
/// Returns std::nullopt when `data` is shorter than the header (a truncated or
/// foreign file), in which case it carries no usable device information. Note
/// this does not validate headerLength/headerVersion — see
/// isPipelineCacheAcceptable, which applies the full check.
[[nodiscard]] auto parsePipelineCacheHeader(std::span<const std::byte> data)
    -> std::optional<PipelineCacheHeader>;

/// True when `data` is a well-formed pipeline cache built for `identity`:
/// long enough for the header, headerLength/headerVersion exactly as Vulkan
/// specifies, and vendorID/deviceID/pipelineCacheUUID all matching. When this
/// returns false the caller must create an empty cache rather than hand the
/// blob to the driver.
[[nodiscard]] auto isPipelineCacheAcceptable(
    std::span<const std::byte> data,
    const PipelineCacheIdentity &identity) -> bool;

/// Directory used for this library's per-user cache files: %LOCALAPPDATA%,
/// ~/Library/Caches, or $XDG_CACHE_HOME / ~/.cache depending on platform.
/// Returns std::nullopt when no platform location can be determined, leaving
/// the caller to decide on a fallback.
[[nodiscard]] auto defaultCacheDir() -> std::optional<std::filesystem::path>;

/// Path of the pipeline-cache file for `appName` under `dir`. `appName` is
/// expected to be already sanitised (see sanitizeCacheName).
[[nodiscard]] auto pipelineCachePathIn(const std::filesystem::path &dir,
                                       const std::string &appName)
    -> std::filesystem::path;

/// Path of the pipeline-cache file for `appName` under defaultCacheDir(),
/// falling back to the current working directory when no platform location is
/// available.
[[nodiscard]] auto defaultPipelineCachePath(const std::string &appName)
    -> std::filesystem::path;

/// Remove abandoned temp files (the "<name>.tmp.<pid>" siblings written by
/// writeFileAtomically) left by a process killed between write and rename.
/// Only removes temps older than `staleAfter` so a concurrently-running
/// instance mid-write isn't disturbed. Best-effort: every error is swallowed,
/// and a missing/unreadable directory is not an error.
void sweepStaleTemps(const std::filesystem::path &target,
                     std::chrono::seconds staleAfter = std::chrono::minutes(1));

/// Write `data` to `path` crash-safely: create the parent directory, write to
/// a PID-suffixed sibling temp file, flush it to disk, then rename over `path`.
/// A kill mid-write therefore leaves either the old file or the new one, never
/// a truncated file — which matters because a truncated cache fails the header
/// check on the next run and silently costs a full pipeline recompile.
///
/// Returns false on any failure, having cleaned up its temp file; persistence
/// is best-effort and a false return is never fatal.
[[nodiscard]] auto writeFileAtomically(const std::filesystem::path &path,
                                       std::span<const std::byte> data) -> bool;

/// Read an entire file into memory. Returns std::nullopt when the file is
/// missing, empty, or unreadable — all of which mean "no usable cache".
[[nodiscard]] auto readFileFully(const std::filesystem::path &path)
    -> std::optional<std::vector<std::byte>>;

/// Persistence for one pipeline-cache file. Holds no Vulkan handles, so it can
/// be constructed and exercised without a device.
class PipelineCacheStore {
 public:
  /// `cacheFile` is the path of the cache blob itself (see
  /// defaultPipelineCachePath).
  explicit PipelineCacheStore(std::filesystem::path cacheFile);

  /// Load the cache blob for `identity`, or std::nullopt when there is none,
  /// it is unreadable, or its header does not match `identity`. Sweeps stale
  /// temp files from a previous crashed run as a side effect.
  [[nodiscard]] auto load(const PipelineCacheIdentity &identity) const
      -> std::optional<std::vector<std::byte>>;

  /// Persist a cache blob obtained from vkGetPipelineCacheData. Best-effort.
  [[nodiscard]] auto save(std::span<const std::byte> data) const -> bool;

  [[nodiscard]] auto path() const noexcept -> const std::filesystem::path & {
    return cacheFile;
  }

 private:
  std::filesystem::path cacheFile;
};

}  // namespace vkutil
