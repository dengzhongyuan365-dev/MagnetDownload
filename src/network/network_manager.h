 // 基础设施 byteBuffer
 #include <span>
 #include <string_view>
 #include <cstddef>
 #include <cstdint>
 #include <vector>
 #include <algorithm>
 #include <functional>
 #include <optional>
 #include <memory>
 namespace networkcore
 {

    class ByteBuffer
    {
        public:
         // 使用std::byte处理二进制数据
            void append(std::span<const std::byte> data);
         // 使用std::string_view处理字符串
            void append(std::string_view str)

        // 提供末模板函数处理不同的整数类型

            template<typename T>
            void appendBigEndian(T value);

    };


    // 网路断点抽象
    class IEndpoint
    {
        public:
            virtual ~IEndpoint() = default;

            virtual std::string getIp() const = 0;

            virtual uint16_t getPort() const = 0;
    };

    // tcp链接接口

    class ITCPConnection
    {
        public:
            virtual ~ITCPConnection() = default;

            // 异步数据发送

            virtual void send(std::span<const std::byte> data) = 0;

            virtual void registerDataHandler(
                std::function<void(std::span<const std::byte>)> handler
            ) = 0;
            
            virtual void registerCloseHandler(std::function<void()> handler) = 0;

            virtual void close() = 0;
    };

    // 网络管理器接口

    class INetworkManager
    {
        virtual ~INetworkManager() = default;

        virtual std::shared_ptr<ITCPConnection> connect(std::string_view host, uint16_t port) = 0;

        virtual std::optional<std::shared_ptr<ITCPConnection>> tryConnect(
            std::string_view host, uint16_t port
        ) = 0;

    };
 };

