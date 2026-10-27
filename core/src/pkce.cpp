#include "pkce.h"

namespace tarkov_companion::pkce {
namespace {

constexpr size_t kBlockBytes = 64;
constexpr size_t kLengthFieldBytes = 8;
constexpr uint8_t kPaddingStartByte = 0x80;
constexpr int kBitsPerByte = 8;

// FIPS 180-4, 4.2.2: first 32 bits of the fractional parts of the cube roots of the first 64 primes.
constexpr uint32_t kRoundConstants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

// FIPS 180-4, 5.3.3: first 32 bits of the fractional parts of the square roots of the first 8 primes.
constexpr uint32_t kInitialHash[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

constexpr uint32_t rotateRight(uint32_t value, int bits) { return (value >> bits) | (value << (32 - bits)); }

// The single-letter working variables and the sigma functions are named as in
// FIPS 180-4, 6.2.2, so the code can be checked against the standard line by line.
void compressBlock(uint32_t (&hash)[8], const uint8_t* block) {
  uint32_t messageSchedule[64];
  for (int index = 0; index < 16; ++index) {
    const uint8_t* word = block + index * 4;
    messageSchedule[index] =
        (uint32_t{word[0]} << 24) | (uint32_t{word[1]} << 16) | (uint32_t{word[2]} << 8) | word[3];
  }
  for (int index = 16; index < 64; ++index) {
    uint32_t sigma0 = rotateRight(messageSchedule[index - 15], 7) ^ rotateRight(messageSchedule[index - 15], 18) ^
                      (messageSchedule[index - 15] >> 3);
    uint32_t sigma1 = rotateRight(messageSchedule[index - 2], 17) ^ rotateRight(messageSchedule[index - 2], 19) ^
                      (messageSchedule[index - 2] >> 10);
    messageSchedule[index] = messageSchedule[index - 16] + sigma0 + messageSchedule[index - 7] + sigma1;
  }

  uint32_t a = hash[0], b = hash[1], c = hash[2], d = hash[3];
  uint32_t e = hash[4], f = hash[5], g = hash[6], h = hash[7];
  for (int index = 0; index < 64; ++index) {
    uint32_t bigSigma1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
    uint32_t choice = (e & f) ^ (~e & g);
    uint32_t firstSum = h + bigSigma1 + choice + kRoundConstants[index] + messageSchedule[index];
    uint32_t bigSigma0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
    uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    uint32_t secondSum = bigSigma0 + majority;

    h = g;
    g = f;
    f = e;
    e = d + firstSum;
    d = c;
    c = b;
    b = a;
    a = firstSum + secondSum;
  }

  hash[0] += a;
  hash[1] += b;
  hash[2] += c;
  hash[3] += d;
  hash[4] += e;
  hash[5] += f;
  hash[6] += g;
  hash[7] += h;
}

}  // namespace

std::array<uint8_t, kSha256DigestBytes> sha256(std::string_view data) {
  uint32_t hash[8];
  std::copy(std::begin(kInitialHash), std::end(kInitialHash), hash);

  const auto* bytes = reinterpret_cast<const uint8_t*>(data.data());
  size_t fullBlockCount = data.size() / kBlockBytes;
  for (size_t block = 0; block < fullBlockCount; ++block) {
    compressBlock(hash, bytes + block * kBlockBytes);
  }

  // Padding: the rest of the message, 0x80, zeros, then the bit length, big-endian.
  uint8_t tail[2 * kBlockBytes] = {};
  size_t restOfMessageBytes = data.size() - fullBlockCount * kBlockBytes;
  std::copy(bytes + fullBlockCount * kBlockBytes, bytes + data.size(), tail);
  tail[restOfMessageBytes] = kPaddingStartByte;

  size_t tailBytes = restOfMessageBytes + 1 + kLengthFieldBytes <= kBlockBytes ? kBlockBytes : 2 * kBlockBytes;
  uint64_t bitLength = static_cast<uint64_t>(data.size()) * kBitsPerByte;
  for (size_t index = 0; index < kLengthFieldBytes; ++index) {
    tail[tailBytes - 1 - index] = static_cast<uint8_t>(bitLength >> (index * kBitsPerByte));
  }
  for (size_t offset = 0; offset < tailBytes; offset += kBlockBytes) {
    compressBlock(hash, tail + offset);
  }

  std::array<uint8_t, kSha256DigestBytes> digest{};
  for (size_t word = 0; word < 8; ++word) {
    for (size_t byte = 0; byte < 4; ++byte) {
      digest[word * 4 + byte] = static_cast<uint8_t>(hash[word] >> (24 - byte * kBitsPerByte));
    }
  }

  return digest;
}

std::string base64UrlWithoutPadding(const uint8_t* data, size_t length) {
  static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  constexpr int kBitsPerCharacter = 6;
  constexpr uint32_t kCharacterBitsMask = 0x3F;

  std::string encoded;
  uint32_t pendingBits = 0;
  int pendingBitCount = 0;
  for (size_t index = 0; index < length; ++index) {
    pendingBits = (pendingBits << kBitsPerByte) | data[index];
    pendingBitCount += kBitsPerByte;
    while (pendingBitCount >= kBitsPerCharacter) {
      pendingBitCount -= kBitsPerCharacter;
      encoded.push_back(kAlphabet[(pendingBits >> pendingBitCount) & kCharacterBitsMask]);
    }
  }
  if (pendingBitCount > 0) {
    encoded.push_back(kAlphabet[(pendingBits << (kBitsPerCharacter - pendingBitCount)) & kCharacterBitsMask]);
  }

  return encoded;
}

std::string verifierFromRandomBytes(const uint8_t* verifierEntropyBytes) {
  return base64UrlWithoutPadding(verifierEntropyBytes, kVerifierEntropyBytes);
}

std::string s256ChallengeFor(std::string_view verifier) {
  auto digest = sha256(verifier);

  return base64UrlWithoutPadding(digest.data(), digest.size());
}

}  // namespace tarkov_companion::pkce
