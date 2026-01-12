#pragma once

#include "dch_types.h"
#include "bencode_types.h"
#include "magnet_types.h"

#include <string>
#include <vector>
#include <optional>
#include <variant>

namespace magnet::protocols {

// ============================================================================
// KRPC 协议字段名常量
// ============================================================================

namespace krpc {
    // 顶层字段
    constexpr const char* kTransactionId = "t";      // 事务 ID
    constexpr const char* kMessageType = "y";        // 消息类型
    constexpr const char* kQueryMethod = "q";        // 查询方法名
    constexpr const char* kArguments = "a";          // 查询参数
    constexpr const char* kResponse = "r";           // 响应数据
    constexpr const char* kError = "e";              // 错误信息
    
    // 消息类型值
    constexpr const char* kTypeQuery = "q";          // 查询
    constexpr const char* kTypeResponse = "r";       // 响应
    constexpr const char* kTypeError = "e";          // 错误
    
    // 查询方法名
    constexpr const char* kMethodPing = "ping";
    constexpr const char* kMethodFindNode = "find_node";
    constexpr const char* kMethodGetPeers = "get_peers";
    constexpr const char* kMethodAnnouncePeer = "announce_peer";
    
    // 参数/响应字段
    constexpr const char* kNodeId = "id";            // 节点 ID
    constexpr const char* kTarget = "target";        // 目标节点 ID
    constexpr const char* kInfoHash = "info_hash";   // 文件哈希
    constexpr const char* kToken = "token";          // 令牌
    constexpr const char* kPort = "port";            // 端口
    constexpr const char* kImpliedPort = "implied_port";  // 隐含端口
    constexpr const char* kNodes = "nodes";          // 紧凑节点列表
    constexpr const char* kValues = "values";        // Peer 列表
} // namespace krpc

/**
 * @brief Peer 信息（IP + 端口）
 */
struct PeerInfo {
    std::string ip;
    uint16_t port;
    
    PeerInfo() : port(0) {}
    PeerInfo(const std::string& ip_, uint16_t port_) : ip(ip_), port(port_) {}
    
    std::string toString() const {
        return ip + ":" + std::to_string(port);
    }
};

/**
 * @brief DHT 错误信息
 */
struct DhtError {
    DhtErrorCode code;
    std::string message;
    
    DhtError() : code(DhtErrorCode::GENERIC) {}
    DhtError(DhtErrorCode c, const std::string& msg) : code(c), message(msg) {}
};

/**
 * @class DhtMessage
 * @brief DHT KRPC 协议消息
 * 
 * DHT 使用 KRPC 协议进行节点间通信，消息格式为 Bencode 编码的字典。
 * 
 * 消息类型：
 * - 查询 (Query): y = "q"
 * - 响应 (Response): y = "r"  
 * - 错误 (Error): y = "e"
 * 
 * 查询类型：
 * - ping: 检查节点是否在线
 * - find_node: 查找指定 NodeId 附近的节点
 * - get_peers: 查找拥有指定 InfoHash 文件的 Peer
 * - announce_peer: 宣告自己拥有某个文件
 * 
 * 使用示例：
 * @code
 * // 创建 get_peers 查询
 * NodeId my_id = NodeId::random();
 * InfoHash hash = ...;
 * auto msg = DhtMessage::createGetPeers(my_id, hash);
 * 
 * // 编码并发送
 * std::vector<uint8_t> data = msg.encode();
 * udp_client.send(endpoint, data);
 * 
 * // 解析响应
 * auto response = DhtMessage::parse(received_data);
 * if (response && response->isResponse()) {
 *     auto peers = response->getPeers();
 *     auto nodes = response->getNodes();
 * }
 * @endcode
 */
class DhtMessage {
public:
    // ========================================================================
    // 构造函数
    // ========================================================================
    
    DhtMessage() = default;
    
    // ========================================================================
    // 静态工厂方法 - 创建查询消息
    // ========================================================================
    
    /**
     * @brief 创建 ping 查询
     * @param my_id 本节点的 NodeId
     * @return ping 消息
     * 
     * 用途：检查目标节点是否在线
     * 响应：对方返回自己的 NodeId
     */
    static DhtMessage createPing(const NodeId& my_id);
    
    /**
     * @brief 创建 find_node 查询
     * @param my_id 本节点的 NodeId
     * @param target 要查找的目标 NodeId
     * @return find_node 消息
     * 
     * 用途：查找离目标 NodeId 最近的节点
     * 响应：返回 K 个最近的节点（紧凑格式）
     */
    static DhtMessage createFindNode(const NodeId& my_id, const NodeId& target);
    
    /**
     * @brief 创建 get_peers 查询
     * @param my_id 本节点的 NodeId
     * @param info_hash 要查找的文件 InfoHash
     * @return get_peers 消息
     * 
     * 用途：查找拥有指定文件的 Peer
     * 响应：
     * - 如果知道 Peer，返回 values (Peer 列表)
     * - 如果不知道，返回 nodes (更近的节点)
     * - 同时返回 token (用于后续 announce_peer)
     */
    static DhtMessage createGetPeers(const NodeId& my_id, const InfoHash& info_hash);
    
    /**
     * @brief 创建 announce_peer 查询
     * @param my_id 本节点的 NodeId
     * @param info_hash 文件的 InfoHash
     * @param port 本节点提供文件的端口
     * @param token 从 get_peers 响应中获得的 token
     * @param implied_port 是否使用 UDP 源端口作为 peer 端口
     * @return announce_peer 消息
     * 
     * 用途：宣告自己拥有某个文件
     */
    static DhtMessage createAnnouncePeer(const NodeId& my_id, 
                                          const InfoHash& info_hash,
                                          uint16_t port,
                                          const std::string& token,
                                          bool implied_port = false);
    
    // ========================================================================
    // 静态工厂方法 - 创建响应消息
    // ========================================================================
    
    /**
     * @brief 创建 ping 响应
     */
    static DhtMessage createPingResponse(const std::string& transaction_id, 
                                          const NodeId& my_id);
    
    /**
     * @brief 创建 find_node 响应
     */
    static DhtMessage createFindNodeResponse(const std::string& transaction_id,
                                              const NodeId& my_id,
                                              const std::vector<DhtNode>& nodes);
    
    /**
     * @brief 创建 get_peers 响应（有 Peer）
     */
    static DhtMessage createGetPeersResponseWithPeers(const std::string& transaction_id,
                                                       const NodeId& my_id,
                                                       const std::string& token,
                                                       const std::vector<PeerInfo>& peers);
    
    /**
     * @brief 创建 get_peers 响应（有更近的节点）
     */
    static DhtMessage createGetPeersResponseWithNodes(const std::string& transaction_id,
                                                       const NodeId& my_id,
                                                       const std::string& token,
                                                       const std::vector<DhtNode>& nodes);
    
    /**
     * @brief 创建错误响应
     */
    static DhtMessage createError(const std::string& transaction_id,
                                   DhtErrorCode code,
                                   const std::string& message);
    
    // ========================================================================
    // 解析方法
    // ========================================================================
    
    /**
     * @brief 从 Bencode 值解析消息
     * @param value Bencode 字典
     * @return 解析后的消息，失败返回 nullopt
     */
    static std::optional<DhtMessage> parse(const BencodeValue& value);
    
    /**
     * @brief 从字节数组解析消息
     * @param data 原始字节数据
     * @return 解析后的消息，失败返回 nullopt
     */
    static std::optional<DhtMessage> parse(const std::vector<uint8_t>& data);
    
    // ========================================================================
    // 编码方法
    // ========================================================================
    
    /**
     * @brief 编码为 Bencode 值
     */
    BencodeValue toBencode() const;
    
    /**
     * @brief 编码为字节数组
     */
    std::vector<uint8_t> encode() const;
    
    // ========================================================================
    // 类型检查
    // ========================================================================
    
    bool isQuery() const { return type_ == DhtMessageType::QUERY; }
    bool isResponse() const { return type_ == DhtMessageType::RESPONSE; }
    bool isError() const { return type_ == DhtMessageType::DHT_ERROR; }
    
    DhtMessageType type() const { return type_; }
    DhtQueryType queryType() const { return query_type_; }
    
    // ========================================================================
    // 字段访问
    // ========================================================================
    
    /** @brief 获取事务 ID */
    const std::string& transactionId() const { return transaction_id_; }
    
    /** @brief 设置事务 ID */
    void setTransactionId(const std::string& tid) { transaction_id_ = tid; }
    
    /** @brief 获取发送方 NodeId（从响应中） */
    const NodeId& senderId() const { return sender_id_; }
    
    /** @brief 获取目标 NodeId（find_node 查询） */
    const NodeId& targetId() const { return target_id_; }
    
    /** @brief 获取 InfoHash（get_peers/announce_peer） */
    const InfoHash& infoHash() const { return info_hash_; }
    
    /** @brief 获取 token（get_peers 响应/announce_peer 查询） */
    const std::string& token() const { return token_; }
    
    /** @brief 获取端口（announce_peer） */
    uint16_t port() const { return port_; }
    
    /** @brief 是否使用 implied port */
    bool impliedPort() const { return implied_port_; }
    
    // ========================================================================
    // 响应数据提取
    // ========================================================================
    
    /**
     * @brief 获取响应中的节点列表
     * @return 节点列表（从 "nodes" 紧凑格式解析）
     */
    std::vector<DhtNode> getNodes() const;
    
    /**
     * @brief 获取响应中的 Peer 列表
     * @return Peer 列表（从 "values" 紧凑格式解析）
     */
    std::vector<PeerInfo> getPeers() const;
    
    /**
     * @brief 是否包含 Peer 列表
     */
    bool hasPeers() const { return !peers_data_.empty(); }
    
    /**
     * @brief 是否包含节点列表
     */
    bool hasNodes() const { return !nodes_data_.empty(); }
    
    /**
     * @brief 获取错误信息
     */
    const DhtError& error() const { return error_; }
    
    // ========================================================================
    // 工具方法
    // ========================================================================
    
    /**
     * @brief 生成随机事务 ID
     * @param length ID 长度（默认 2 字节）
     */
    static std::string generateTransactionId(size_t length = 2);

private:
    // 消息类型
    DhtMessageType type_ = DhtMessageType::QUERY;
    DhtQueryType query_type_ = DhtQueryType::PING;
    
    // 通用字段
    std::string transaction_id_;    // "t" - 事务 ID
    NodeId sender_id_;              // 发送方 NodeId（查询中的 "a.id" 或响应中的 "r.id"）
    
    // 查询特定字段
    NodeId target_id_;              // "a.target" - find_node 的目标
    InfoHash info_hash_;            // "a.info_hash" - get_peers/announce_peer 的目标
    std::string token_;             // "a.token" 或 "r.token"
    uint16_t port_ = 0;             // "a.port" - announce_peer 的端口
    bool implied_port_ = false;     // "a.implied_port"
    
    // 响应数据（原始格式，延迟解析）
    std::string nodes_data_;        // "r.nodes" - 紧凑节点数据
    std::vector<std::string> peers_data_;  // "r.values" - 紧凑 Peer 数据列表
    
    // 错误信息
    DhtError error_;
    
    // 辅助方法
    static std::string nodesToCompact(const std::vector<DhtNode>& nodes);
    static std::vector<std::string> peersToCompact(const std::vector<PeerInfo>& peers);
};

} // namespace magnet::protocols

