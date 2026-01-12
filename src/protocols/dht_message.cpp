// MagnetDownload - DHT Message Implementation
// KRPC protocol message construction and parsing

#include "magnet/protocols/dht_message.h"
#include "magnet/protocols/bencode.h"
#include "magnet/utils/logger.h"

#include <random>
#include <sstream>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

namespace magnet::protocols {

// Helper macros for logging
#define LOG_DEBUG(msg) magnet::utils::Logger::instance().debug(std::string("[DhtMessage] ") + msg)
#define LOG_WARN(msg) magnet::utils::Logger::instance().warn(std::string("[DhtMessage] ") + msg)

// ============================================================================
// Static Factory Methods - Create Query Messages
// ============================================================================

DhtMessage DhtMessage::createPing(const NodeId& my_id) {
    DhtMessage msg;
    msg.type_ = DhtMessageType::QUERY;
    msg.query_type_ = DhtQueryType::PING;
    msg.transaction_id_ = generateTransactionId();
    msg.sender_id_ = my_id;
    return msg;
}

DhtMessage DhtMessage::createFindNode(const NodeId& my_id, const NodeId& target) {
    DhtMessage msg;
    msg.type_ = DhtMessageType::QUERY;
    msg.query_type_ = DhtQueryType::FIND_NODE;
    msg.transaction_id_ = generateTransactionId();
    msg.sender_id_ = my_id;
    msg.target_id_ = target;
    return msg;
}

DhtMessage DhtMessage::createGetPeers(const NodeId& my_id, const InfoHash& info_hash) {
    DhtMessage msg;
    msg.type_ = DhtMessageType::QUERY;
    msg.query_type_ = DhtQueryType::GET_PEERS;
    msg.transaction_id_ = generateTransactionId();
    msg.sender_id_ = my_id;
    msg.info_hash_ = info_hash;
    return msg;
}

DhtMessage DhtMessage::createAnnouncePeer(const NodeId& my_id,
                                           const InfoHash& info_hash,
                                           uint16_t port,
                                           const std::string& token,
                                           bool implied_port) {
    DhtMessage msg;
    msg.type_ = DhtMessageType::QUERY;
    msg.query_type_ = DhtQueryType::ANNOUNCE_PEER;
    msg.transaction_id_ = generateTransactionId();
    msg.sender_id_ = my_id;
    msg.info_hash_ = info_hash;
    msg.port_ = port;
    msg.token_ = token;
    msg.implied_port_ = implied_port;
    return msg;
}

// ============================================================================
// Static Factory Methods - Create Response Messages
// ============================================================================

DhtMessage DhtMessage::createPingResponse(const std::string& transaction_id,
                                           const NodeId& my_id) {
    DhtMessage msg;
    msg.type_ = DhtMessageType::RESPONSE;
    msg.transaction_id_ = transaction_id;
    msg.sender_id_ = my_id;
    return msg;
}

DhtMessage DhtMessage::createFindNodeResponse(const std::string& transaction_id,
                                               const NodeId& my_id,
                                               const std::vector<DhtNode>& nodes) {
    DhtMessage msg;
    msg.type_ = DhtMessageType::RESPONSE;
    msg.transaction_id_ = transaction_id;
    msg.sender_id_ = my_id;
    msg.nodes_data_ = nodesToCompact(nodes);
    return msg;
}

DhtMessage DhtMessage::createGetPeersResponseWithPeers(const std::string& transaction_id,
                                                        const NodeId& my_id,
                                                        const std::string& token,
                                                        const std::vector<PeerInfo>& peers) {
    DhtMessage msg;
    msg.type_ = DhtMessageType::RESPONSE;
    msg.transaction_id_ = transaction_id;
    msg.sender_id_ = my_id;
    msg.token_ = token;
    msg.peers_data_ = peersToCompact(peers);
    return msg;
}

DhtMessage DhtMessage::createGetPeersResponseWithNodes(const std::string& transaction_id,
                                                        const NodeId& my_id,
                                                        const std::string& token,
                                                        const std::vector<DhtNode>& nodes) {
    DhtMessage msg;
    msg.type_ = DhtMessageType::RESPONSE;
    msg.transaction_id_ = transaction_id;
    msg.sender_id_ = my_id;
    msg.token_ = token;
    msg.nodes_data_ = nodesToCompact(nodes);
    return msg;
}

DhtMessage DhtMessage::createError(const std::string& transaction_id,
                                    DhtErrorCode code,
                                    const std::string& message) {
    DhtMessage msg;
    msg.type_ = DhtMessageType::DHT_ERROR;
    msg.transaction_id_ = transaction_id;
    msg.error_ = DhtError(code, message);
    return msg;
}

// ============================================================================
// Parsing Methods
// ============================================================================

std::optional<DhtMessage> DhtMessage::parse(const std::vector<uint8_t>& data) {
    std::string_view sv(reinterpret_cast<const char*>(data.data()), data.size());
    auto bencode_result = Bencode::decode(sv);
    if (!bencode_result) {
        LOG_WARN("Failed to decode Bencode data");
        return std::nullopt;
    }
    return parse(*bencode_result);
}

std::optional<DhtMessage> DhtMessage::parse(const BencodeValue& value) {
    if (!value.isDict()) {
        LOG_WARN("DHT message must be a dictionary");
        return std::nullopt;
    }
    
    const auto& dict = value.asDict();
    DhtMessage msg;
    
    // Parse transaction ID
    auto t_it = dict.find(krpc::kTransactionId);
    if (t_it == dict.end() || !t_it->second.isString()) {
        LOG_WARN("Missing or invalid transaction ID");
        return std::nullopt;
    }
    msg.transaction_id_ = t_it->second.asString();
    
    // Parse message type
    auto y_it = dict.find(krpc::kMessageType);
    if (y_it == dict.end() || !y_it->second.isString()) {
        LOG_WARN("Missing or invalid message type");
        return std::nullopt;
    }
    
    const std::string& y = y_it->second.asString();
    
    if (y == krpc::kTypeQuery) {
        // Query message
        msg.type_ = DhtMessageType::QUERY;
        
        // Parse query method
        auto q_it = dict.find(krpc::kQueryMethod);
        if (q_it == dict.end() || !q_it->second.isString()) {
            LOG_WARN("Missing query method");
            return std::nullopt;
        }
        
        const std::string& q = q_it->second.asString();
        if (q == krpc::kMethodPing) {
            msg.query_type_ = DhtQueryType::PING;
        } else if (q == krpc::kMethodFindNode) {
            msg.query_type_ = DhtQueryType::FIND_NODE;
        } else if (q == krpc::kMethodGetPeers) {
            msg.query_type_ = DhtQueryType::GET_PEERS;
        } else if (q == krpc::kMethodAnnouncePeer) {
            msg.query_type_ = DhtQueryType::ANNOUNCE_PEER;
        } else {
            LOG_WARN("Unknown query type: " + q);
            return std::nullopt;
        }
        
        // Parse arguments
        auto a_it = dict.find(krpc::kArguments);
        if (a_it == dict.end() || !a_it->second.isDict()) {
            LOG_WARN("Missing query arguments");
            return std::nullopt;
        }
        
        const auto& args = a_it->second.asDict();
        
        // Parse sender ID
        auto id_it = args.find(krpc::kNodeId);
        if (id_it != args.end() && id_it->second.isString()) {
            const std::string& id_str = id_it->second.asString();
            if (id_str.size() == NodeId::s_KNodeSize) {
                NodeId::ByteArray bytes;
                std::memcpy(bytes.data(), id_str.data(), NodeId::s_KNodeSize);
                msg.sender_id_ = NodeId(bytes);
            }
        }
        
        // Parse target for find_node
        if (msg.query_type_ == DhtQueryType::FIND_NODE) {
            auto target_it = args.find(krpc::kTarget);
            if (target_it != args.end() && target_it->second.isString()) {
                const std::string& target_str = target_it->second.asString();
                if (target_str.size() == NodeId::s_KNodeSize) {
                    NodeId::ByteArray bytes;
                    std::memcpy(bytes.data(), target_str.data(), NodeId::s_KNodeSize);
                    msg.target_id_ = NodeId(bytes);
                }
            }
        }
        
        // Parse info_hash for get_peers/announce_peer
        if (msg.query_type_ == DhtQueryType::GET_PEERS || 
            msg.query_type_ == DhtQueryType::ANNOUNCE_PEER) {
            auto hash_it = args.find(krpc::kInfoHash);
            if (hash_it != args.end() && hash_it->second.isString()) {
                const std::string& hash_str = hash_it->second.asString();
                if (hash_str.size() == InfoHash::HASHSIZE) {
                    InfoHash::ByteArray bytes;
                    std::memcpy(bytes.data(), hash_str.data(), InfoHash::HASHSIZE);
                    msg.info_hash_ = InfoHash(bytes);
                }
            }
        }
        
        // Parse announce_peer specific fields
        if (msg.query_type_ == DhtQueryType::ANNOUNCE_PEER) {
            auto token_it = args.find(krpc::kToken);
            if (token_it != args.end() && token_it->second.isString()) {
                msg.token_ = token_it->second.asString();
            }
            
            auto port_it = args.find(krpc::kPort);
            if (port_it != args.end() && port_it->second.isInt()) {
                msg.port_ = static_cast<uint16_t>(port_it->second.asInt());
            }
            
            auto implied_it = args.find(krpc::kImpliedPort);
            if (implied_it != args.end() && implied_it->second.isInt()) {
                msg.implied_port_ = (implied_it->second.asInt() != 0);
            }
        }
        
    } else if (y == krpc::kTypeResponse) {
        // Response message
        msg.type_ = DhtMessageType::RESPONSE;
        
        // Parse response data
        auto r_it = dict.find(krpc::kResponse);
        if (r_it == dict.end() || !r_it->second.isDict()) {
            LOG_WARN("Missing response data");
            return std::nullopt;
        }
        
        const auto& resp = r_it->second.asDict();
        
        // Parse sender ID
        auto id_it = resp.find(krpc::kNodeId);
        if (id_it != resp.end() && id_it->second.isString()) {
            const std::string& id_str = id_it->second.asString();
            if (id_str.size() == NodeId::s_KNodeSize) {
                NodeId::ByteArray bytes;
                std::memcpy(bytes.data(), id_str.data(), NodeId::s_KNodeSize);
                msg.sender_id_ = NodeId(bytes);
            }
        }
        
        // Parse token
        auto token_it = resp.find(krpc::kToken);
        if (token_it != resp.end() && token_it->second.isString()) {
            msg.token_ = token_it->second.asString();
        }
        
        // Parse nodes (compact format)
        auto nodes_it = resp.find(krpc::kNodes);
        if (nodes_it != resp.end() && nodes_it->second.isString()) {
            msg.nodes_data_ = nodes_it->second.asString();
        }
        
        // Parse values (peer list)
        auto values_it = resp.find(krpc::kValues);
        if (values_it != resp.end() && values_it->second.isList()) {
            const auto& values = values_it->second.asList();
            for (const auto& v : values) {
                if (v.isString()) {
                    msg.peers_data_.push_back(v.asString());
                }
            }
        }
        
    } else if (y == krpc::kTypeError) {
        // Error message
        msg.type_ = DhtMessageType::DHT_ERROR;
        
        // Parse error
        auto e_it = dict.find(krpc::kError);
        if (e_it != dict.end() && e_it->second.isList()) {
            const auto& err = e_it->second.asList();
            if (err.size() >= 2) {
                if (err[0].isInt()) {
                    msg.error_.code = static_cast<DhtErrorCode>(err[0].asInt());
                }
                if (err[1].isString()) {
                    msg.error_.message = err[1].asString();
                }
            }
        }
        
    } else {
        LOG_WARN("Unknown message type: " + y);
        return std::nullopt;
    }
    
    return msg;
}

// ============================================================================
// Encoding Methods
// ============================================================================

BencodeValue DhtMessage::toBencode() const {
    BencodeDict dict;
    
    // Transaction ID
    dict[krpc::kTransactionId] = BencodeValue(transaction_id_);
    
    if (type_ == DhtMessageType::QUERY) {
        dict[krpc::kMessageType] = BencodeValue(krpc::kTypeQuery);
        
        // Query method
        const char* method = nullptr;
        switch (query_type_) {
            case DhtQueryType::PING: method = krpc::kMethodPing; break;
            case DhtQueryType::FIND_NODE: method = krpc::kMethodFindNode; break;
            case DhtQueryType::GET_PEERS: method = krpc::kMethodGetPeers; break;
            case DhtQueryType::ANNOUNCE_PEER: method = krpc::kMethodAnnouncePeer; break;
        }
        dict[krpc::kQueryMethod] = BencodeValue(method);
        
        // Arguments
        BencodeDict args;
        args[krpc::kNodeId] = BencodeValue(sender_id_.toString());
        
        if (query_type_ == DhtQueryType::FIND_NODE) {
            args[krpc::kTarget] = BencodeValue(target_id_.toString());
        }
        
        if (query_type_ == DhtQueryType::GET_PEERS || 
            query_type_ == DhtQueryType::ANNOUNCE_PEER) {
            // Convert InfoHash bytes to string
            const auto& hash_bytes = info_hash_.bytes();
            std::string hash_str(reinterpret_cast<const char*>(hash_bytes.data()), hash_bytes.size());
            args[krpc::kInfoHash] = BencodeValue(hash_str);
        }
        
        if (query_type_ == DhtQueryType::ANNOUNCE_PEER) {
            args[krpc::kPort] = BencodeValue(static_cast<BencodeInt>(port_));
            args[krpc::kToken] = BencodeValue(token_);
            if (implied_port_) {
                args[krpc::kImpliedPort] = BencodeValue(static_cast<BencodeInt>(1));
            }
        }
        
        dict[krpc::kArguments] = BencodeValue(args);
        
    } else if (type_ == DhtMessageType::RESPONSE) {
        dict[krpc::kMessageType] = BencodeValue(krpc::kTypeResponse);
        
        BencodeDict resp;
        resp[krpc::kNodeId] = BencodeValue(sender_id_.toString());
        
        if (!token_.empty()) {
            resp[krpc::kToken] = BencodeValue(token_);
        }
        
        if (!nodes_data_.empty()) {
            resp[krpc::kNodes] = BencodeValue(nodes_data_);
        }
        
        if (!peers_data_.empty()) {
            BencodeList values;
            for (const auto& peer : peers_data_) {
                values.push_back(BencodeValue(peer));
            }
            resp[krpc::kValues] = BencodeValue(values);
        }
        
        dict[krpc::kResponse] = BencodeValue(resp);
        
    } else if (type_ == DhtMessageType::DHT_ERROR) {
        dict[krpc::kMessageType] = BencodeValue(krpc::kTypeError);
        
        BencodeList err;
        err.push_back(BencodeValue(static_cast<BencodeInt>(error_.code)));
        err.push_back(BencodeValue(error_.message));
        dict[krpc::kError] = BencodeValue(err);
    }
    
    return BencodeValue(dict);
}

std::vector<uint8_t> DhtMessage::encode() const {
    std::string encoded = Bencode::encode(toBencode());
    return std::vector<uint8_t>(encoded.begin(), encoded.end());
}

// ============================================================================
// Response Data Extraction
// ============================================================================

std::vector<DhtNode> DhtMessage::getNodes() const {
    std::vector<DhtNode> result;
    
    if (nodes_data_.empty()) {
        return result;
    }
    
    auto compact_nodes = CompactNodeInfo::parseNodes(nodes_data_);
    for (const auto& cn : compact_nodes) {
        result.push_back(cn.toDhtNode());
    }
    
    return result;
}

std::vector<PeerInfo> DhtMessage::getPeers() const {
    std::vector<PeerInfo> result;
    
    for (const auto& peer_data : peers_data_) {
        if (peer_data.size() >= CompactPeerInfo::s_kCompactPeerSize) {
            auto peer = CompactPeerInfo::fromBytes(
                reinterpret_cast<const uint8_t*>(peer_data.data()),
                peer_data.size()
            );
            if (peer) {
                result.emplace_back(peer->ipString(), peer->hostPort());
            }
        }
    }
    
    return result;
}

// ============================================================================
// Utility Methods
// ============================================================================

std::string DhtMessage::generateTransactionId(size_t length) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 255);
    
    std::string tid;
    tid.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        tid.push_back(static_cast<char>(dis(gen)));
    }
    return tid;
}

std::string DhtMessage::nodesToCompact(const std::vector<DhtNode>& nodes) {
    std::string result;
    result.reserve(nodes.size() * CompactNodeInfo::s_kCompactNodeSize);
    
    for (const auto& node : nodes) {
        // NodeId (20 bytes)
        const auto& id_bytes = node.id_.bytes();
        result.append(reinterpret_cast<const char*>(id_bytes.data()), id_bytes.size());
        
        // IP address (4 bytes, network byte order)
        // Parse IP string to uint32_t
        uint32_t ip = 0;
        unsigned int a = 0, b = 0, c = 0, d = 0;
#ifdef _MSC_VER
        if (sscanf_s(node.ip_.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
#else
        if (sscanf(node.ip_.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
#endif
            ip = htonl((a << 24) | (b << 16) | (c << 8) | d);
        }
        result.append(reinterpret_cast<const char*>(&ip), 4);
        
        // Port (2 bytes, network byte order)
        uint16_t port = htons(node.port_);
        result.append(reinterpret_cast<const char*>(&port), 2);
    }
    
    return result;
}

std::vector<std::string> DhtMessage::peersToCompact(const std::vector<PeerInfo>& peers) {
    std::vector<std::string> result;
    result.reserve(peers.size());
    
    for (const auto& peer : peers) {
        std::string compact;
        compact.reserve(CompactPeerInfo::s_kCompactPeerSize);
        
        // IP address (4 bytes, network byte order)
        uint32_t ip = 0;
        unsigned int a = 0, b = 0, c = 0, d = 0;
#ifdef _MSC_VER
        if (sscanf_s(peer.ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
#else
        if (sscanf(peer.ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
#endif
            ip = htonl((a << 24) | (b << 16) | (c << 8) | d);
        }
        compact.append(reinterpret_cast<const char*>(&ip), 4);
        
        // Port (2 bytes, network byte order)
        uint16_t port = htons(peer.port);
        compact.append(reinterpret_cast<const char*>(&port), 2);
        
        result.push_back(compact);
    }
    
    return result;
}

} // namespace magnet::protocols

