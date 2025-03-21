#include "../network/network_manager.h"
namespace Protocol
{
    enum class MessageType : uint8_t
    {
        Choke = 0 ,
        Unchoke = 1,
        Interested = 2,
        NotInterested = 3,
        Have = 4,
        Bitfield = 5,
        Request = 6,
        Piece = 7,
        Cancel = 8
    };


    class Message
    {
        public:
            virtual ~Message() = default;
            virtual MessageType getMsgType() const = 0;
            virtual networkcore::ByteBuffer serialize() const = 0;
    };


    // 不同消息类型的具体实现

    class PieceMessage: public Message
    {
        public:

        // 虚拟 函数 "Protocol::PieceMessage::~PieceMessage" 的异常规范与重写 函数 "Protocol::Message::~Message" 的异常规范不兼容
            ~PieceMessage() = default;

            PieceMessage(uint32_t index, uint32_t begin, std::span<const std::byte> data);

            MessageType getMsgType() const override
            {
                return MessageType::Piece;
            }

            networkcore::ByteBuffer serialize() const override {};

            uint32_t getIndex() const {return index_ ; }

            uint32_t getBegin() const { return begin_ ; }

            std::span<const std::byte> getData() const { return data_;}
            
        private:
            uint32_t index_;
            uint32_t begin_;

            std::vector<std::byte> data_;

    };

    class MessageFactory 
    {
        using MessageVariant = 
        std::variant<std::monostate, HandshakeMessage, BitfieldMessage, RequestMessage, PieceMessage, std::shared_ptr<Message>>;



        static std::optional<MessageVariant> parse(std::span<const std::byte> data);





    }


};