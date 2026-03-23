#include <cstddef>
#include <cstdint>
#include <vector>
#include <stdexcept>

#ifdef _WIN32
#  include <algorithm>
#  include <limits>

#  ifndef NOMINMAX
#    define NOMINMAX
#  endif

#  include <windows.h>
#  include <bcrypt.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#  include <cstdlib>
#elif defined(__linux__)
#  include <algorithm>
#  include <cerrno>

#  include <sys/random.h>
#endif

#include "mcp/security/crypto_random.hpp"

namespace mcp::security
{
namespace detail
{

// NOLINTNEXTLINE(bugprone-implicit-widening-of-multiplication-result)
constexpr std::size_t kChunkSize = 256U * 1024U;

}  // namespace detail

auto cryptoRandomBytes(std::size_t length) -> std::vector<std::uint8_t>
{
  std::vector<std::uint8_t> bytes(length);
  if (bytes.empty())
  {
    return bytes;
  }

#ifdef _WIN32
  std::size_t offset = 0;
  constexpr std::size_t kWindowsChunkMax = static_cast<std::size_t>(std::numeric_limits<ULONG>::max());
  while (offset < bytes.size())
  {
    const std::size_t remaining = bytes.size() - offset;
    const std::size_t chunkSize = std::min(remaining, kWindowsChunkMax);

    const NTSTATUS status = BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(bytes.data() + offset), static_cast<ULONG>(chunkSize), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status != 0)
    {
      throw std::runtime_error("Failed to generate cryptographic random bytes using BCryptGenRandom");
    }

    offset += chunkSize;
  }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
  // NOLINTNEXTLINE(misc-include-cleaner) - arc4random_buf is from <cstdlib> on BSD/macOS
  arc4random_buf(bytes.data(), bytes.size());
#elif defined(__linux__)
  std::size_t offset = 0;
  while (offset < bytes.size())
  {
    const std::size_t remaining = bytes.size() - offset;
    const std::size_t chunkSize = std::min(remaining, detail::kChunkSize);

    const ssize_t readCount = getrandom(bytes.data() + offset, chunkSize, 0);
    if (readCount < 0)
    {
      if (errno == EINTR)
      {
        continue;
      }

      throw std::runtime_error("Failed to generate cryptographic random bytes using getrandom");
    }

    if (readCount == 0)
    {
      throw std::runtime_error("getrandom returned zero bytes unexpectedly");
    }

    offset += static_cast<std::size_t>(readCount);
  }
#else
  throw std::runtime_error("No supported cryptographic random source is available on this platform");
#endif

  return bytes;
}

}  // namespace mcp::security
