#include <doctest/doctest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include "PipelineCacheStore.h"

// The pipeline-cache persistence logic lives in PipelineCacheStore precisely so
// it can be tested here: none of these cases needs a Vulkan instance, a
// device, a window, or a display. Before the split this logic was unreachable
// from the test binary — it was buried in VulkanContext::createPipelineCache /
// savePipelineCache, which cannot be constructed in a unit test.

namespace {

namespace fs = std::filesystem;

constexpr size_t kHeaderVersionOffset = sizeof(uint32_t);
constexpr size_t kVendorIDOffset = kHeaderVersionOffset + sizeof(uint32_t);
constexpr size_t kDeviceIDOffset = kVendorIDOffset + sizeof(uint32_t);
constexpr size_t kUUIDOffset = kDeviceIDOffset + sizeof(uint32_t);
constexpr size_t kHeaderSize = kUUIDOffset + VK_UUID_SIZE;

void writeScalarAt(std::vector<std::byte> &data, size_t offset,
                   uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

constexpr uint32_t kTestVendor = 0x10DE;
constexpr uint32_t kTestDevice = 0x2684;

// A well-formed cache blob: correct header, then arbitrary payload bytes.
auto makeValidCacheBlob(uint8_t uuidSeed = 0xAB) -> std::vector<std::byte> {
  std::vector<std::byte> data(kHeaderSize + 16, std::byte{0x42});
  writeScalarAt(data, 0, static_cast<uint32_t>(kHeaderSize));
  writeScalarAt(data, kHeaderVersionOffset,
                VK_PIPELINE_CACHE_HEADER_VERSION_ONE);
  writeScalarAt(data, kVendorIDOffset, kTestVendor);
  writeScalarAt(data, kDeviceIDOffset, kTestDevice);
  for (size_t i = 0; i < VK_UUID_SIZE; ++i) {
    data[kUUIDOffset + i] = static_cast<std::byte>(uuidSeed + i);
  }
  return data;
}

auto testUuid(uint8_t seed = 0xAB) -> std::vector<std::byte> {
  std::vector<std::byte> uuid(VK_UUID_SIZE);
  for (size_t i = 0; i < VK_UUID_SIZE; ++i) {
    uuid[i] = static_cast<std::byte>(seed + i);
  }
  return uuid;
}

// Owns a unique scratch directory removed on destruction, so cases can't leak
// files into the repo or into each other.
class TempDir {
 public:
  TempDir() {
    path_ = fs::temp_directory_path() /
            ("VulkanWindowCacheTest-" +
             std::to_string(static_cast<uint64_t>(
                 std::chrono::steady_clock::now().time_since_epoch().count())) +
             "-" + std::to_string(counter_++));
    fs::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  TempDir(const TempDir &) = delete;
  auto operator=(const TempDir &) -> TempDir & = delete;
  TempDir(TempDir &&) = delete;
  auto operator=(TempDir &&) -> TempDir & = delete;

  [[nodiscard]] auto path() const -> const fs::path & { return path_; }

 private:
  fs::path path_;
  static inline uint64_t counter_{0};
};

auto identityFor(const std::vector<std::byte> &uuid)
    -> vkutil::PipelineCacheIdentity {
  return vkutil::PipelineCacheIdentity{
      kTestVendor, kTestDevice, reinterpret_cast<const uint8_t *>(uuid.data())};
}

}  // namespace

// ---- parsePipelineCacheHeader ----------------------------------------------

TEST_CASE("parsePipelineCacheHeader rejects data shorter than a header") {
  const std::vector<std::byte> empty;
  CHECK_FALSE(vkutil::parsePipelineCacheHeader(empty).has_value());

  const std::vector<std::byte> partial(kHeaderSize - 1, std::byte{0});
  CHECK_FALSE(vkutil::parsePipelineCacheHeader(partial).has_value());
}

TEST_CASE("parsePipelineCacheHeader extracts vendor, device and UUID") {
  const auto blob = makeValidCacheBlob(0x11);
  const auto header = vkutil::parsePipelineCacheHeader(blob);

  REQUIRE(header.has_value());
  CHECK(header->vendorID == kTestVendor);
  CHECK(header->deviceID == kTestDevice);
  for (size_t i = 0; i < VK_UUID_SIZE; ++i) {
    CHECK(header->pipelineCacheUUID[i] == static_cast<uint8_t>(0x11 + i));
  }
}

// ---- isPipelineCacheAcceptable ---------------------------------------------

TEST_CASE("isPipelineCacheAcceptable accepts a matching blob") {
  const auto blob = makeValidCacheBlob();
  const auto uuid = testUuid();
  CHECK(vkutil::isPipelineCacheAcceptable(blob, identityFor(uuid)));
}

TEST_CASE("isPipelineCacheAcceptable rejects a truncated blob") {
  const auto uuid = testUuid();
  std::vector<std::byte> truncated(kHeaderSize - 1, std::byte{0});
  CHECK_FALSE(vkutil::isPipelineCacheAcceptable(truncated, identityFor(uuid)));
}

TEST_CASE("isPipelineCacheAcceptable rejects a wrong vendor or device") {
  const auto uuid = testUuid();

  SUBCASE("vendor mismatch") {
    auto blob = makeValidCacheBlob();
    writeScalarAt(blob, kVendorIDOffset, kTestVendor + 1);
    CHECK_FALSE(vkutil::isPipelineCacheAcceptable(blob, identityFor(uuid)));
  }

  SUBCASE("device mismatch") {
    auto blob = makeValidCacheBlob();
    writeScalarAt(blob, kDeviceIDOffset, kTestDevice + 1);
    CHECK_FALSE(vkutil::isPipelineCacheAcceptable(blob, identityFor(uuid)));
  }
}

TEST_CASE("isPipelineCacheAcceptable rejects a UUID from another device") {
  const auto blob = makeValidCacheBlob(0xAB);
  const auto otherUuid = testUuid(0x01);  // same vendor/device, new driver UUID
  CHECK_FALSE(vkutil::isPipelineCacheAcceptable(blob, identityFor(otherUuid)));
}

TEST_CASE("isPipelineCacheAcceptable rejects a bad header length or version") {
  const auto uuid = testUuid();

  SUBCASE("headerLength not equal to the spec header size") {
    auto blob = makeValidCacheBlob();
    writeScalarAt(blob, 0, static_cast<uint32_t>(kHeaderSize) + 4);
    CHECK_FALSE(vkutil::isPipelineCacheAcceptable(blob, identityFor(uuid)));
  }

  SUBCASE("headerVersion not VK_PIPELINE_CACHE_HEADER_VERSION_ONE") {
    auto blob = makeValidCacheBlob();
    writeScalarAt(blob, kHeaderVersionOffset,
                  VK_PIPELINE_CACHE_HEADER_VERSION_ONE + 1);
    CHECK_FALSE(vkutil::isPipelineCacheAcceptable(blob, identityFor(uuid)));
  }
}

TEST_CASE("isPipelineCacheAcceptable rejects a null UUID pointer") {
  const auto blob = makeValidCacheBlob();
  const vkutil::PipelineCacheIdentity noUuid{kTestVendor, kTestDevice, nullptr};
  CHECK_FALSE(vkutil::isPipelineCacheAcceptable(blob, noUuid));
}

// ---- path derivation -------------------------------------------------------

TEST_CASE("pipelineCachePathIn namespaces the file by app name") {
  const fs::path dir = "C:/cache-root";
  const auto path = vkutil::pipelineCachePathIn(dir, "MyApp");

  CHECK(path.parent_path().filename() == "MyApp");
  CHECK(path.filename() == "pipeline_cache.bin");
  CHECK(path.parent_path().parent_path() == dir);
}

TEST_CASE("defaultPipelineCachePath is never empty") {
  // Whatever the platform cache location resolves to (or doesn't), the result
  // must be a usable relative-or-absolute path ending in our file name.
  const auto path = vkutil::defaultPipelineCachePath("VulkanWindowTests");
  CHECK_FALSE(path.empty());
  CHECK(path.filename() == "pipeline_cache.bin");
}

// ---- readFileFully / writeFileAtomically -----------------------------------

TEST_CASE("readFileFully reports missing and empty files as no data") {
  const TempDir tmp;

  CHECK_FALSE(vkutil::readFileFully(tmp.path() / "absent.bin").has_value());

  const fs::path empty = tmp.path() / "empty.bin";
  { std::ofstream(empty, std::ios::binary); }
  CHECK_FALSE(vkutil::readFileFully(empty).has_value());
}

TEST_CASE("writeFileAtomically round-trips through readFileFully") {
  const TempDir tmp;
  const fs::path target = tmp.path() / "nested" / "cache.bin";
  const auto blob = makeValidCacheBlob();

  REQUIRE(vkutil::writeFileAtomically(target, blob));
  CHECK(fs::exists(target));

  const auto readBack = vkutil::readFileFully(target);
  REQUIRE(readBack.has_value());
  CHECK(*readBack == blob);
}

TEST_CASE("writeFileAtomically replaces an existing file and leaves no temp") {
  const TempDir tmp;
  const fs::path target = tmp.path() / "cache.bin";

  REQUIRE(vkutil::writeFileAtomically(target, makeValidCacheBlob(0xAA)));
  const std::vector<std::byte> replacement(64, std::byte{0x7F});
  REQUIRE(vkutil::writeFileAtomically(target, replacement));

  const auto readBack = vkutil::readFileFully(target);
  REQUIRE(readBack.has_value());
  CHECK(*readBack == replacement);

  // The temp-then-rename dance must not leave the sibling behind.
  for (const auto &entry : fs::directory_iterator(tmp.path())) {
    CHECK(entry.path().filename().string().find(".tmp.") == std::string::npos);
  }
}

TEST_CASE("writeFileAtomically handles a bare relative path") {
  // Regression guard for the no-platform-cache-dir fallback, which returns
  // exactly "pipeline_cache.bin" — a path whose parent_path() is EMPTY.
  // fs::create_directories("") sets EINVAL on that empty path; the error_code
  // overload does not throw, so this must still write successfully rather than
  // aborting or losing the save. Run from a scratch dir so it lands somewhere
  // disposable.
  const TempDir tmp;
  const fs::path previous = fs::current_path();
  fs::current_path(tmp.path());

  const auto blob = makeValidCacheBlob();
  const bool wrote = vkutil::writeFileAtomically(fs::path{"cache.bin"}, blob);

  fs::current_path(previous);

  CHECK(wrote);
  const auto readBack = vkutil::readFileFully(tmp.path() / "cache.bin");
  REQUIRE(readBack.has_value());
  CHECK(*readBack == blob);
}

TEST_CASE("writeFileAtomically replaces the contents completely") {
  // Contract check for the crash-safe write: after it returns, the target holds
  // exactly the new blob (no stale tail from the previous file) and no temp
  // file survives.
  //
  // Coverage limit, stated plainly because the name of this case used to imply
  // more: this does NOT verify that a reader never observes a partial file.
  // That was attempted two ways and neither works:
  //
  //  - A threaded observer is both flaky (the write usually finishes before the
  //    next sample, so the observation count is 0) and weak (a direct write
  //    that happens to complete is indistinguishable from an atomic one).
  //  - Blocking the temp path, making the target read-only, and taking an
  //    exclusive lock were each tried as a deterministic discriminator. All
  //    three make BOTH a temp+rename write and a direct write fail, so none of
  //    them separates the two implementations.
  //
  // The atomicity guarantee therefore rests on the implementation being a
  // temp-then-rename (see writeFileAtomically in PipelineCacheStore.h) rather
  // than on any test here. What is asserted below does catch the realistic
  // regression: a write that truncates or appends in place instead of
  // replacing.
  const TempDir tmp;
  const fs::path target = tmp.path() / "cache.bin";

  // Start from a pre-existing, differently-sized file so a stale remainder
  // would be visible if the write truncated in place instead of replacing.
  const std::vector<std::byte> oldContents(4096, std::byte{0x11});
  REQUIRE(vkutil::writeFileAtomically(target, oldContents));
  REQUIRE(fs::file_size(target) == oldContents.size());

  const std::vector<std::byte> newContents(64, std::byte{0x22});
  CHECK(vkutil::writeFileAtomically(target, newContents));

  // Exactly the new blob: not appended, not partially overwritten, no stale
  // tail.
  const auto readBack = vkutil::readFileFully(target);
  REQUIRE(readBack.has_value());
  CHECK(*readBack == newContents);
  CHECK(fs::file_size(target) == newContents.size());

  // And the directory holds only the target: the temp was renamed away, never
  // left behind.
  size_t entries = 0;
  for (const auto &entry : fs::directory_iterator(tmp.path())) {
    (void)entry;
    ++entries;
  }
  CHECK(entries == 1);
}

// ---- sweepStaleTemps -------------------------------------------------------

TEST_CASE("sweepStaleTemps removes old temps and keeps recent ones") {
  const TempDir tmp;
  const fs::path target = tmp.path() / "pipeline_cache.bin";

  const fs::path stale = tmp.path() / "pipeline_cache.bin.tmp.1234";
  const fs::path fresh = tmp.path() / "pipeline_cache.bin.tmp.5678";
  const fs::path unrelated = tmp.path() / "someone-elses-file.bin";
  for (const auto &p : {stale, fresh, unrelated}) {
    std::ofstream(p, std::ios::binary) << "x";
  }

  // Backdate the "stale" temp beyond the threshold. The clock used by
  // last_write_time is not the system clock, so shift it relatively.
  const auto oldMtime = fs::last_write_time(stale) - std::chrono::hours(1);
  fs::last_write_time(stale, oldMtime);

  vkutil::sweepStaleTemps(target, std::chrono::seconds(60));

  CHECK_FALSE(fs::exists(stale));  // old temp: swept
  CHECK(fs::exists(fresh));        // recent: possibly a live writer
  CHECK(fs::exists(unrelated));    // never ours to delete
}

TEST_CASE("sweepStaleTemps tolerates a missing directory") {
  const TempDir tmp;
  // Must not throw or crash when the cache directory doesn't exist yet.
  CHECK_NOTHROW(vkutil::sweepStaleTemps(tmp.path() / "not-created" / "c.bin"));
}

// ---- PipelineCacheStore ----------------------------------------------------

TEST_CASE("PipelineCacheStore load returns nullopt when no file exists") {
  const TempDir tmp;
  const vkutil::PipelineCacheStore store(tmp.path() / "absent.bin");
  const auto uuid = testUuid();

  CHECK_FALSE(store.load(identityFor(uuid)).has_value());
}

TEST_CASE("PipelineCacheStore round-trips a cache for the same device") {
  const TempDir tmp;
  const vkutil::PipelineCacheStore store(tmp.path() / "cache.bin");
  const auto blob = makeValidCacheBlob();
  const auto uuid = testUuid();

  REQUIRE(store.save(blob));

  const auto loaded = store.load(identityFor(uuid));
  REQUIRE(loaded.has_value());
  CHECK(*loaded == blob);
}

TEST_CASE("PipelineCacheStore load rejects a cache for a different device") {
  const TempDir tmp;
  const vkutil::PipelineCacheStore store(tmp.path() / "cache.bin");
  const auto blob = makeValidCacheBlob();
  REQUIRE(store.save(blob));

  // A different driver UUID for the same hardware must not reuse the blob.
  const auto otherUuid = testUuid(0x01);
  CHECK_FALSE(store.load(identityFor(otherUuid)).has_value());
}

TEST_CASE("PipelineCacheStore save rejects empty data") {
  const TempDir tmp;
  const vkutil::PipelineCacheStore store(tmp.path() / "cache.bin");

  CHECK_FALSE(store.save(std::span<const std::byte>{}));
  CHECK_FALSE(fs::exists(store.path()));
}

TEST_CASE("PipelineCacheStore exposes the configured path") {
  const TempDir tmp;
  const fs::path expected = tmp.path() / "cache.bin";
  const vkutil::PipelineCacheStore store(expected);

  CHECK(store.path() == expected);
}
