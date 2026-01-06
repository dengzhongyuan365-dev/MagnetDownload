/**
 * @file test_magnet_uri_parser.cpp
 * @brief Magnet URI 解析器单元测试
 */

#include <gtest/gtest.h>
#include <magnet/protocols/magnet_uri_parser.h>  // 与实际文件名一致

using namespace magnet::protocols;

// ========== 基本解析测试 ==========

TEST(MagnetUriParserTest, ParseSimpleMagnetLink) {
    std::string uri = "magnet:?xt=urn:btih:1234567890abcdef1234567890abcdef12345678";

    auto result = parseMagnetUri(uri);
    ASSERT_TRUE(result.is_ok());

    const auto& info = result.value();
    EXPECT_TRUE(info.info_hash.has_value());
    EXPECT_EQ(info.info_hash->toHex(), "1234567890abcdef1234567890abcdef12345678");
}

TEST(MagnetUriParserTest, ParseWithDisplayName) {
    std::string uri = "magnet:?xt=urn:btih:1234567890abcdef1234567890abcdef12345678&dn=test+file";

    auto result = parseMagnetUri(uri);
    ASSERT_TRUE(result.is_ok());

    const auto& info = result.value();
    EXPECT_EQ(info.display_name, "test file");
}

TEST(MagnetUriParserTest, ParseWithSingleTracker) {
    std::string uri = "magnet:?xt=urn:btih:1234567890abcdef1234567890abcdef12345678"
        "&tr=http://tracker.example.com:8080/announce";

    auto result = parseMagnetUri(uri);
    ASSERT_TRUE(result.is_ok());

    const auto& info = result.value();
    ASSERT_EQ(info.trackers.size(), 1);
    EXPECT_EQ(info.trackers[0], "http://tracker.example.com:8080/announce");
}

TEST(MagnetUriParserTest, ParseWithMultipleTrackers) {
    std::string uri = "magnet:?xt=urn:btih:1234567890abcdef1234567890abcdef12345678"
        "&tr=http://tracker1.com/announce"
        "&tr=http://tracker2.com/announce";

    auto result = parseMagnetUri(uri);
    ASSERT_TRUE(result.is_ok());

    const auto& info = result.value();
    ASSERT_EQ(info.trackers.size(), 2);
    EXPECT_EQ(info.trackers[0], "http://tracker1.com/announce");
    EXPECT_EQ(info.trackers[1], "http://tracker2.com/announce");
}

TEST(MagnetUriParserTest, ParseWithExactLength) {
    std::string uri = "magnet:?xt=urn:btih:1234567890abcdef1234567890abcdef12345678&xl=1048576";

    auto result = parseMagnetUri(uri);
    ASSERT_TRUE(result.is_ok());

    const auto& info = result.value();
    EXPECT_TRUE(info.exact_length.has_value());
    EXPECT_EQ(info.exact_length.value(), 1048576);
}

// ========== 错误情况测试 ==========

TEST(MagnetUriParserTest, ParseEmptyString) {
    auto result = parseMagnetUri("");
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ParseError::EMPTY_URI);
}

TEST(MagnetUriParserTest, ParseInvalidScheme) {
    auto result = parseMagnetUri("http://example.com");
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ParseError::INVALID_SCHEME);
}

TEST(MagnetUriParserTest, ParseMissingInfoHash) {
    auto result = parseMagnetUri("magnet:?dn=test");
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ParseError::MISSING_INFO_HASH);
}

// ========== InfoHash 测试 ==========

TEST(InfoHashTest, FromHexValid) {
    std::string hex = "1234567890abcdef1234567890abcdef12345678";
    auto hash = InfoHash::fromHex(hex);
    EXPECT_TRUE(hash.has_value());
    EXPECT_EQ(hash->toHex(), hex);
}

TEST(InfoHashTest, FromHexInvalidLength) {
    std::string hex = "123456";  // 太短
    auto hash = InfoHash::fromHex(hex);
    EXPECT_FALSE(hash.has_value());
}

TEST(InfoHashTest, FromBase32Valid) {
    std::string base32 = "";
    for (int i = 0; i < 32; i++) {
        base32 += "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"[i % 32];
    }
    auto hash = InfoHash::fromBase32(base32);
    EXPECT_TRUE(!hash.has_value());
}

TEST(InfoHashTest, IsValidCheck) {
    auto hash = InfoHash::fromHex("0000000000000000000000000000000000000000");
    ASSERT_TRUE(hash.has_value());
    EXPECT_FALSE(hash->is_valid());  // 全0无效

    hash = InfoHash::fromHex("1234567890abcdef1234567890abcdef12345678");
    ASSERT_TRUE(hash.has_value());
    EXPECT_TRUE(hash->is_valid());
}

// ========== 工具函数测试 ==========

TEST(MagnetUriParserTest, ExtractInfoHashValid) {
    std::string uri = "magnet:?xt=urn:btih:1234567890abcdef1234567890abcdef12345678";
    auto hash = extractInfoHash(uri);
    EXPECT_TRUE(hash.has_value());
    EXPECT_EQ(hash->toHex(), "1234567890abcdef1234567890abcdef12345678");
}

TEST(MagnetUriParserTest, IsValidMagnetUri) {
    EXPECT_TRUE(isValidMagnetUri("magnet:?xt=urn:btih:1234567890abcdef1234567890abcdef12345678"));
    EXPECT_FALSE(isValidMagnetUri("http://example.com"));
    EXPECT_FALSE(isValidMagnetUri(""));
}

TEST(MagnetUriParserTest, BuildMagnetUri) {
    MagnetInfo info;
    info.info_hash = InfoHash::fromHex("1234567890abcdef1234567890abcdef12345678").value();
    info.display_name = "test file";

    std::string uri = buildMagnetUri(info);
    EXPECT_FALSE(uri.empty());
    EXPECT_TRUE(uri.find("magnet:?") == 0);
    EXPECT_TRUE(uri.find("dn=test+file") != std::string::npos);
}

TEST(MagnetUriParserTest, ParseAndBuildRoundTrip) {
    std::string original = "magnet:?xt=urn:btih:1234567890abcdef1234567890abcdef12345678&dn=test";

    auto result = parseMagnetUri(original);
    ASSERT_TRUE(result.is_ok());

    std::string rebuilt = buildMagnetUri(result.value());
    auto reparse_result = parseMagnetUri(rebuilt);
    ASSERT_TRUE(reparse_result.is_ok());

    EXPECT_EQ(reparse_result.value().info_hash->toHex(),
        result.value().info_hash->toHex());
}