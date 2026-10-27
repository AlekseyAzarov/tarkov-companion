// PKCE (RFC 7636). SHA-256 (FIPS 180-4) is implemented here because the core
// links no crypto library; it only hashes, never encrypts.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace tarkov_companion::pkce {

inline constexpr size_t kSha256DigestBytes = 32;
// 32 random bytes: a 43-character verifier, the shortest RFC 7636 allows,
// with 256 bits of entropy.
inline constexpr size_t kVerifierEntropyBytes = 32;

std::array<uint8_t, kSha256DigestBytes> sha256(std::string_view data);

std::string base64UrlWithoutPadding(const uint8_t* data, size_t length);

std::string verifierFromRandomBytes(const uint8_t* verifierEntropyBytes);
std::string s256ChallengeFor(std::string_view verifier);

}  // namespace tarkov_companion::pkce
