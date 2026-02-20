#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mcp::detail
{

constexpr std::size_t kBase64AlphabetSize = 64;
constexpr std::size_t kDecodeTableSize = 256;
constexpr std::size_t kUppercaseLetterCount = 26;
constexpr std::size_t kLowercaseLetterCount = 26;
constexpr std::size_t kDigitCount = 10;
constexpr std::size_t kBase64UrlValueMinus = 62;
constexpr std::size_t kBase64UrlValueUnderscore = 63;
constexpr std::uint8_t kMask6Bits = 0x3FU;
constexpr std::uint8_t kMask8Bits = 0xFFU;
constexpr std::uint8_t kMask4Bits = 0x0FU;
constexpr std::uint32_t kShift16 = 16U;
constexpr std::uint32_t kShift8 = 8U;

inline auto encodeBase64UrlNoPad(std::string_view bytes) -> std::string
{
  static constexpr std::array<char, kBase64AlphabetSize> kBase64UrlAlphabet = []() -> std::array<char, kBase64AlphabetSize>
  {
    std::array<char, kBase64AlphabetSize> table {};
    const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    for (std::size_t i = 0; i < kBase64AlphabetSize; ++i)
    {
      table[i] = alphabet[i];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-constant-array-index)
    }
    return table;
  }();

  constexpr auto kShift18 = 18U;
  constexpr auto kShift12 = 12U;
  constexpr auto kShift6 = 6U;

  const std::size_t inputLength = bytes.size();
  if (inputLength == 0)
  {
    return {};
  }

  const std::size_t outputLength = ((inputLength + 2U) / 3U) * 4U;
  std::string encoded;
  encoded.reserve(outputLength);

  std::size_t idx = 0;
  while (idx + 2U < inputLength)
  {
    const std::uint32_t triple = (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[idx])) << 16U)
      | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[idx + 1U])) << 8U) | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[idx + 2U]));

    encoded.push_back(kBase64UrlAlphabet[(triple >> kShift18) & kMask6Bits]);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    encoded.push_back(kBase64UrlAlphabet[(triple >> kShift12) & kMask6Bits]);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    encoded.push_back(kBase64UrlAlphabet[(triple >> kShift6) & kMask6Bits]);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    encoded.push_back(kBase64UrlAlphabet[triple & kMask6Bits]);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    idx += 3U;
  }

  const std::size_t remaining = inputLength - idx;
  if (remaining == 1U)
  {
    const std::uint32_t triple = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[idx])) << 16U;
    encoded.push_back(kBase64UrlAlphabet[(triple >> kShift18) & kMask6Bits]);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    encoded.push_back(kBase64UrlAlphabet[(triple >> kShift12) & kMask6Bits]);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
  }
  else if (remaining == 2U)
  {
    const std::uint32_t triple =
      (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[idx])) << 16U) | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[idx + 1U])) << 8U);
    encoded.push_back(kBase64UrlAlphabet[(triple >> kShift18) & kMask6Bits]);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    encoded.push_back(kBase64UrlAlphabet[(triple >> kShift12) & kMask6Bits]);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    encoded.push_back(kBase64UrlAlphabet[(triple >> kShift6) & kMask6Bits]);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
  }

  return encoded;
}

inline auto decodeBase64UrlNoPad(std::string_view text) -> std::optional<std::string>
{
  const std::size_t inputLength = text.size();
  if (inputLength == 0)
  {
    return std::string {};
  }

  static const auto kDecodeTable = []() -> std::array<std::int8_t, kDecodeTableSize>
  {
    std::array<std::int8_t, kDecodeTableSize> table {};
    table.fill(-1);
    for (std::size_t i = 0; i < kUppercaseLetterCount; ++i)
    {
      table[static_cast<std::size_t>('A' + i)] = static_cast<std::int8_t>(i);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    }
    for (std::size_t i = 0; i < kLowercaseLetterCount; ++i)
    {
      table[static_cast<std::size_t>('a' + i)] = static_cast<std::int8_t>(kUppercaseLetterCount + i);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    }
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
    for (std::size_t i = 0; i < kDigitCount; ++i)
    {
      table[static_cast<std::size_t>('0' + i)] = static_cast<std::int8_t>(kUppercaseLetterCount + kLowercaseLetterCount + i);
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    table[static_cast<std::size_t>('-')] = static_cast<std::int8_t>(kBase64UrlValueMinus);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    table[static_cast<std::size_t>('_')] = static_cast<std::int8_t>(kBase64UrlValueUnderscore);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    return table;
  }();

  for (const char character : text)
  {
    const auto byte = static_cast<unsigned char>(character);
    if (kDecodeTable[byte] < 0)  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    {
      return std::nullopt;
    }
  }

  const std::size_t remainder = inputLength % 4;
  if (remainder == 1)
  {
    return std::nullopt;
  }

  std::size_t outputLength = (inputLength / 4U) * 3U;
  if (remainder == 2)
  {
    outputLength += 1;
  }
  else if (remainder == 3)
  {
    outputLength += 2;
  }

  std::string decoded;
  decoded.reserve(outputLength);

  std::size_t idx = 0;
  while (idx + 3 < inputLength)
  {
    const auto byte0 = static_cast<unsigned char>(text[idx]);
    const auto byte1 = static_cast<unsigned char>(text[idx + 1]);
    const auto byte2 = static_cast<unsigned char>(text[idx + 2]);
    const auto byte3 = static_cast<unsigned char>(text[idx + 3]);
    const std::uint32_t quad = (static_cast<std::uint32_t>(kDecodeTable[byte0]) << 18U)  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
      | (static_cast<std::uint32_t>(kDecodeTable[byte1]) << 12U)  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
      | (static_cast<std::uint32_t>(kDecodeTable[byte2]) << 6U) | static_cast<std::uint32_t>(kDecodeTable[byte3]);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)

    decoded.push_back(static_cast<char>((quad >> kShift16) & kMask8Bits));
    decoded.push_back(static_cast<char>((quad >> kShift8) & kMask8Bits));
    decoded.push_back(static_cast<char>(quad & kMask8Bits));
    idx += 4;
  }

  if (remainder == 2)
  {
    const auto char0 = static_cast<unsigned char>(text[idx]);
    const auto char1 = static_cast<unsigned char>(text[idx + 1]);
    const auto val0 = static_cast<std::uint32_t>(static_cast<std::uint8_t>(kDecodeTable[char0]));  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    const auto val1 = static_cast<std::uint32_t>(static_cast<std::uint8_t>(kDecodeTable[char1]));  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    decoded.push_back(static_cast<char>((val0 << 2U) | (val1 >> 4U)));
  }
  else if (remainder == 3)
  {
    const auto char0 = static_cast<unsigned char>(text[idx]);
    const auto char1 = static_cast<unsigned char>(text[idx + 1]);
    const auto char2 = static_cast<unsigned char>(text[idx + 2]);
    const auto val0 = static_cast<std::uint32_t>(static_cast<std::uint8_t>(kDecodeTable[char0]));  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    const auto val1 = static_cast<std::uint32_t>(static_cast<std::uint8_t>(kDecodeTable[char1]));  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    const auto val2 = static_cast<std::uint32_t>(static_cast<std::uint8_t>(kDecodeTable[char2]));  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    decoded.push_back(static_cast<char>((val0 << 2U) | (val1 >> 4U)));
    decoded.push_back(static_cast<char>(((val1 & kMask4Bits) << 4U) | (val2 >> 2U)));
  }

  return decoded;
}

}  // namespace mcp::detail
