#include "pkce.h"

#include <gtest/gtest.h>

#include <string>

namespace tarkov_companion::pkce {
namespace {

std::string hex(const std::array<uint8_t, kSha256DigestBytes>& digest) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string text;
  for (uint8_t byte : digest) {
    text.push_back(kDigits[byte >> 4]);
    text.push_back(kDigits[byte & 0xF]);
  }

  return text;
}

TEST(Sha256, NistVectors) {
  EXPECT_EQ(hex(sha256("")), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(hex(sha256("abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(hex(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  EXPECT_EQ(hex(sha256(std::string(1000000, 'a'))),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// Padding is where SHA-256 implementations break: messages that end right
// before, at and after a block boundary, or leave no room for the length.
TEST(Sha256, BlockBoundaries) {
  const std::pair<size_t, const char*> cases[] = {
      {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
      {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
      {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
      {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
      {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
      {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
      {120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
  };
  for (const auto& [length, expected] : cases) EXPECT_EQ(hex(sha256(std::string(length, 'a'))), expected) << length;
}

TEST(Base64Url, Rfc4648VectorsWithoutPadding) {
  auto encode = [](std::string_view text) {
    return base64UrlWithoutPadding(reinterpret_cast<const uint8_t*>(text.data()), text.size());
  };
  EXPECT_EQ(encode(""), "");
  EXPECT_EQ(encode("f"), "Zg");
  EXPECT_EQ(encode("fo"), "Zm8");
  EXPECT_EQ(encode("foo"), "Zm9v");
  EXPECT_EQ(encode("foob"), "Zm9vYg");
  EXPECT_EQ(encode("fooba"), "Zm9vYmE");
  EXPECT_EQ(encode("foobar"), "Zm9vYmFy");
  const uint8_t urlAlphabet[] = {0xfb, 0xff};
  EXPECT_EQ(base64UrlWithoutPadding(urlAlphabet, sizeof(urlAlphabet)), "-_8") << "'-' and '_', not '+' and '/'";
}

// RFC 7636, appendix B: the worked example.
TEST(Pkce, Rfc7636Example) {
  const uint8_t random[kVerifierEntropyBytes] = {116, 24,  223, 180, 151, 153, 224, 37,  79,  250, 96,
                                                 125, 216, 173, 187, 186, 22,  212, 37,  77,  105, 214,
                                                 191, 240, 91,  88,  5,   88,  83,  132, 141, 121};
  std::string verifier = verifierFromRandomBytes(random);
  EXPECT_EQ(verifier, "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk");
  EXPECT_EQ(s256ChallengeFor(verifier), "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
}

}  // namespace
}  // namespace tarkov_companion::pkce
