#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mcp::detail
{

/**
 * @brief Encodes bytes to base64url (URL-safe alphabet, no padding).
 *
 * Uses the URL-safe base64 alphabet: A-Z, a-z, 0-9, -, _
 * Does not include padding characters (=).
 *
 * @param bytes The input bytes to encode.
 * @return std::string The base64url-encoded string.
 */
inline auto encodeBase64UrlNoPad(std::string_view bytes) -> std::string
{
  static constexpr char kBase64UrlAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

  const std::size_t inputLength = bytes.size();
  if (inputLength == 0)
  {
    return {};
  }

  // Calculate output size: ceil(inputLength * 4 / 3), but without padding
  const std::size_t outputLength = ((inputLength + 2U) / 3U) * 4U;
  std::string encoded;
  encoded.reserve(outputLength);

  std::size_t index = 0;
  while (index + 2U < inputLength)
  {
    const std::uint32_t triple = (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index])) << 16U)
      | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index + 1U])) << 8U) | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index + 2U]));

    encoded.push_back(kBase64UrlAlphabet[(triple >> 18U) & 0x3FU]);
    encoded.push_back(kBase64UrlAlphabet[(triple >> 12U) & 0x3FU]);
    encoded.push_back(kBase64UrlAlphabet[(triple >> 6U) & 0x3FU]);
    encoded.push_back(kBase64UrlAlphabet[triple & 0x3FU]);
    index += 3U;
  }

  const std::size_t remainder = inputLength - index;
  if (remainder == 1U)
  {
    const std::uint32_t triple = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index])) << 16U;
    encoded.push_back(kBase64UrlAlphabet[(triple >> 18U) & 0x3FU]);
    encoded.push_back(kBase64UrlAlphabet[(triple >> 12U) & 0x3FU]);
    // No padding for base64url
  }
  else if (remainder == 2U)
  {
    const std::uint32_t triple =
      (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index])) << 16U) | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index + 1U])) << 8U);
    encoded.push_back(kBase64UrlAlphabet[(triple >> 18U) & 0x3FU]);
    encoded.push_back(kBase64UrlAlphabet[(triple >> 12U) & 0x3FU]);
    encoded.push_back(kBase64UrlAlphabet[(triple >> 6U) & 0x3FU]);
    // No padding for base64url
  }

  return encoded;
}

/**
 * @brief Decodes a base64url (URL-safe alphabet, no padding) string.
 *
 * Uses the URL-safe base64 alphabet: A-Z, a-z, 0-9, -, _
 * Does not accept padding characters (=).
 *
 * Decoding is strict:
 * - Rejects inputs with remainder length 1 (invalid base64 quantum)
 * - Rejects characters not in the url-safe alphabet
 * - Rejects padding characters (=)
 *
 * @param text The base64url-encoded string to decode.
 * @return std::optional<std::string> The decoded bytes, or std::nullopt if invalid.
 */
inline auto decodeBase64UrlNoPad(std::string_view text) -> std::optional<std::string>
{
  const std::size_t inputLength = text.size();
  if (inputLength == 0)
  {
    return std::string {};
  }

  // Build reverse lookup table
  static const auto kDecodeTable = []() -> std::array<std::int8_t, 256>
  {
    std::array<std::int8_t, 256> table {};
    table.fill(-1);
    for (std::int8_t i = 0; i < 26; ++i)
    {
      table[static_cast<std::size_t>('A' + i)] = i;
    }
    for (std::int8_t i = 0; i < 26; ++i)
    {
      table[static_cast<std::size_t>('a' + i)] = 26 + i;
    }
    for (std::int8_t i = 0; i < 10; ++i)
    {
      table[static_cast<std::size_t>('0' + i)] = 52 + i;
    }
    table[static_cast<std::size_t>('-')] = 62;
    table[static_cast<std::size_t>('_')] = 63;
    return table;
  }();

  // Check for invalid characters and padding
  for (const char character : text)
  {
    const unsigned char byte = static_cast<unsigned char>(character);
    if (kDecodeTable[byte] < 0)
    {
      // Invalid character (includes '=' padding, whitespace, standard base64 +/, etc.)
      return std::nullopt;
    }
  }

  // Calculate output size
  // For unpadded base64url: every 4 chars decode to 3 bytes
  // Remainder 0: exact multiple of 3 bytes
  // Remainder 2: 1 extra byte
  // Remainder 3: 2 extra bytes
  // Remainder 1: invalid (would decode to partial byte)
  const std::size_t remainder = inputLength % 4;
  if (remainder == 1)
  {
    // Invalid: remainder of 1 would decode to a partial byte
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

  std::size_t index = 0;
  while (index + 3 < inputLength)
  {
    const std::uint32_t quad = (static_cast<std::uint32_t>(kDecodeTable[static_cast<unsigned char>(text[index])]) << 18U)
      | (static_cast<std::uint32_t>(kDecodeTable[static_cast<unsigned char>(text[index + 1])]) << 12U)
      | (static_cast<std::uint32_t>(kDecodeTable[static_cast<unsigned char>(text[index + 2])]) << 6U)
      | static_cast<std::uint32_t>(kDecodeTable[static_cast<unsigned char>(text[index + 3])]);

    decoded.push_back(static_cast<char>((quad >> 16U) & 0xFFU));
    decoded.push_back(static_cast<char>((quad >> 8U) & 0xFFU));
    decoded.push_back(static_cast<char>(quad & 0xFFU));
    index += 4;
  }

  // Handle remaining characters (0, 2, or 3)
  // Remainder 2: 2 chars -> 1 byte: (c0 << 2) | (c1 >> 4)
  // Remainder 3: 3 chars -> 2 bytes: ((c0 << 2) | (c1 >> 4)), (((c1 & 0x0F) << 4) | (c2 >> 2))
  if (remainder == 2)
  {
    const std::uint32_t c0 = static_cast<std::uint32_t>(kDecodeTable[static_cast<unsigned char>(text[index])]);
    const std::uint32_t c1 = static_cast<std::uint32_t>(kDecodeTable[static_cast<unsigned char>(text[index + 1])]);
    decoded.push_back(static_cast<char>((c0 << 2U) | (c1 >> 4U)));
  }
  else if (remainder == 3)
  {
    const std::uint32_t c0 = static_cast<std::uint32_t>(kDecodeTable[static_cast<unsigned char>(text[index])]);
    const std::uint32_t c1 = static_cast<std::uint32_t>(kDecodeTable[static_cast<unsigned char>(text[index + 1])]);
    const std::uint32_t c2 = static_cast<std::uint32_t>(kDecodeTable[static_cast<unsigned char>(text[index + 2])]);
    decoded.push_back(static_cast<char>((c0 << 2U) | (c1 >> 4U)));
    decoded.push_back(static_cast<char>(((c1 & 0x0FU) << 4U) | (c2 >> 2U)));
  }

  return decoded;
}

}  // namespace mcp::detail
