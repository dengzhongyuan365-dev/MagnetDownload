#ifndef MAGNET_URI_PARSER_H
#define MAGNET_URI_PARSER_H


#pragma once

#include "magnet_types.h"
#include <string>

namespace magnet::protocols {

	// ============================================================================
	// 主解析函数声明
	// ============================================================================

	/**
	 * @brief 解析磁力链接URI
	 *
	 * 解析格式如下的磁力链接：
	 * magnet:?xt=urn:btih:HASH&dn=NAME&tr=TRACKER&xl=SIZE
	 *
	 * @param uri 完整的磁力链接字符串
	 * @return 包含解析结果或错误的Result对象
	 *
	 * @example
	 * @code
	 * auto result = parseMagnetUri("magnet:?xt=urn:btih:...");
	 * if (result.is_ok()) {
	 *     auto info = result.value();
	 *     // 使用info
	 * } else {
	 *     auto error = result.error();
	 *     // 处理错误
	 * }
	 * @endcode
	 */
	Result<MagnetInfo, ParseError> parseMagnetUri(const std::string& uri);

	/**
	 * @brief 从MagnetInfo构建磁力链接URI
	 *
	 * 这是parseMagnetUri的逆操作，将结构化的MagnetInfo转换回磁力链接字符串。
	 * 注意：只构建有效的参数，空字段会被忽略。
	 *
	 * @param info 要转换的MagnetInfo结构
	 * @return 磁力链接字符串，如果info无效则返回空字符串
	 *
	 * @example
	 * @code
	 * MagnetInfo info;
	 * // 设置info的字段...
	 * std::string uri = buildMagnetUri(info);
	 * @endcode
	 */
	std::string buildMagnetUri(const MagnetInfo& info);

	// ============================================================================
	// 工具函数声明
	// ============================================================================

	/**
	 * @brief 快速提取磁力链接中的info hash
	 *
	 * 如果只需要哈希值而不需要完整的解析信息，使用此函数更高效。
	 *
	 * @param uri 磁力链接字符串
	 * @return 如果成功提取返回InfoHash，否则返回std::nullopt
	 *
	 * @example
	 * @code
	 * auto hash = extractInfoHash("magnet:?xt=urn:btih:...");
	 * if (hash) {
	 *     std::cout << "Hash: " << hash->toHex() << std::endl;
	 * }
	 * @endcode
	 */
	std::optional<InfoHash> extractInfoHash(const std::string& uri);

	/**
	 * @brief 检查是否是有效的磁力链接
	 *
	 * 快速验证字符串是否是格式正确的磁力链接。
	 * 注意：此函数只验证格式，不验证哈希值的有效性。
	 *
	 * @param uri 要检查的字符串
	 * @return 如果是有效的磁力链接格式返回true
	 *
	 * @example
	 * @code
	 * if (isValidMagnetUri(some_string)) {
	 *     // 是有效的磁力链接
	 * }
	 * @endcode
	 */
	bool isValidMagnetUri(const std::string& uri);

	/**
	 * @brief 将MagnetInfo转换为可读的字符串
	 *
	 * 主要用于调试和日志记录，生成格式化的信息字符串。
	 *
	 * @param info 要转换的MagnetInfo结构
	 * @return 格式化的字符串，包含所有字段的易读表示
	 *
	 * @example
	 * @code
	 * auto result = parseMagnetUri(some_magnet);
	 * if (result.is_ok()) {
	 *     std::cout << toString(result.value()) << std::endl;
	 * }
	 * @endcode
	 */
	std::string toString(const MagnetInfo& info);

} // namespace magnet::protocols

#endif // MAGNET_URI_PARSER_H