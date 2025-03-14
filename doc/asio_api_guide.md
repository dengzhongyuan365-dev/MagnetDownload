# Asio常用接口说明

## 核心概念

### 1. io_context
```cpp
asio::io_context io;
```
* **功能**: Asio的核心，管理所有I/O操作和事件分发
* **用法**: 创建后通过`run()`方法启动事件循环
* **主要方法**:
  * `run()` - 阻塞运行直到所有任务完成
  * `poll()` - 非阻塞执行准备好的处理程序
  * `stop()` - 停止事件循环
  * `restart()` - 重置状态

### 2. 错误处理
```cpp
asio::error_code ec;
```
* **功能**: 错误信息容器
* **用法**: 传递给异步操作作为错误输出
* **主要方法**:
  * `message()` - 获取错误消息字符串
  * `value()` - 获取错误代码

## 计时器接口

### 1. steady_timer
```cpp
asio::steady_timer timer(io_context, asio::chrono::seconds(5));
```
* **功能**: 提供基于稳定时钟的计时器功能
* **主要方法**:
  * `expires_after(duration)` - 设置定时器到期时间(相对)
  * `expires_at(time_point)` - 设置定时器到期时间(绝对)
  * `wait()` - 同步等待定时器到期
  * `async_wait(handler)` - 异步等待定时器到期
  * `cancel()` - 取消定时器

## 网络接口

### 1. IP地址与端点
```cpp
asio::ip::address addr = asio::ip::make_address("127.0.0.1");
asio::ip::tcp::endpoint endpoint(addr, 8080);
```
* **地址类型**:
  * `asio::ip::address` - IP地址基类
  * `asio::ip::address_v4` - IPv4地址
  * `asio::ip::address_v6` - IPv6地址
* **端点类型**:
  * `asio::ip::tcp::endpoint` - TCP端点
  * `asio::ip::udp::endpoint` - UDP端点

### 2. 解析器
```cpp
asio::ip::tcp::resolver resolver(io_context);
auto results = resolver.resolve("www.example.com", "http");
```
* **功能**: 域名解析
* **主要方法**:
  * `resolve(host, service)` - 同步解析主机名和服务
  * `async_resolve(host, service, handler)` - 异步解析

### 3. TCP套接字
```cpp
asio::ip::tcp::socket socket(io_context);
```
* **主要方法**:
  * `connect(endpoint)` - 同步连接
  * `async_connect(endpoint, handler)` - 异步连接
  * `send(buffer)` - 同步发送数据
  * `async_send(buffer, handler)` - 异步发送数据
  * `receive(buffer)` - 同步接收数据
  * `async_receive(buffer, handler)` - 异步接收数据
  * `close()` - 关闭套接字

### 4. TCP接收器
```cpp
asio::ip::tcp::acceptor acceptor(io_context, endpoint);
```
* **功能**: 接受入站TCP连接
* **主要方法**:
  * `accept()` - 同步接受连接
  * `async_accept(handler)` - 异步接受连接
  * `listen()` - 开始监听连接
  * `bind(endpoint)` - 绑定到端点

### 5. UDP套接字
```cpp
asio::ip::udp::socket socket(io_context);
```
* **主要方法**:
  * `send_to(buffer, endpoint)` - 同步发送数据到指定端点
  * `async_send_to(buffer, endpoint, handler)` - 异步发送
  * `receive_from(buffer, endpoint)` - 同步接收数据
  * `async_receive_from(buffer, endpoint, handler)` - 异步接收

## 缓冲区接口

### 1. 缓冲区类型
```cpp
std::vector<char> data(1024);
asio::buffer(data);  // 可变缓冲区
asio::const_buffer(data.data(), data.size());  // 常量缓冲区
```
* **可变缓冲区**: 用于读取操作
* **常量缓冲区**: 用于写入操作
* **动态缓冲区**:
  * `asio::dynamic_vector_buffer` - 基于vector的动态缓冲区
  * `asio::dynamic_string_buffer` - 基于string的动态缓冲区

## 异步编程模型

### 1. 回调风格
```cpp
socket.async_receive(
    asio::buffer(data),
    [](std::error_code ec, std::size_t bytes_transferred) {
        if (!ec) {
            // 处理接收到的数据
        }
    }
);
```

### 2. 协程风格 (C++20)
```cpp
asio::awaitable<void> echo(asio::ip::tcp::socket socket) {
    try {
        char data[1024];
        for (;;) {
            std::size_t n = co_await socket.async_read_some(
                asio::buffer(data), asio::use_awaitable);
            co_await async_write(socket, asio::buffer(data, n), 
                asio::use_awaitable);
        }
    } catch (std::exception& e) {
        // 处理异常
    }
}
```

## 实用示例

### 1. TCP客户端基本流程
```cpp
// 初始化io_context
asio::io_context io;

// 创建socket
asio::ip::tcp::socket socket(io);

// 解析服务器地址
asio::ip::tcp::resolver resolver(io);
auto endpoints = resolver.resolve("www.example.com", "http");

// 连接到服务器
asio::connect(socket, endpoints);

// 发送数据
std::string request = "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n";
socket.write_some(asio::buffer(request));

// 接收响应
std::vector<char> buffer(4096);
size_t bytes_received = socket.read_some(asio::buffer(buffer));

// 关闭连接
socket.close();
```

### 2. TCP服务器基本流程
```cpp
// 初始化服务器
asio::io_context io;
asio::ip::tcp::acceptor acceptor(io, 
    asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 8080));

// 接受连接
asio::ip::tcp::socket client_socket(io);
acceptor.accept(client_socket);

// 处理连接
std::vector<char> buffer(1024);
size_t bytes_received = client_socket.read_some(asio::buffer(buffer));
client_socket.write_some(asio::buffer(buffer, bytes_received));

// 关闭连接
client_socket.close();
```

### 3. 异步TCP服务器
```cpp
void start_accept(asio::ip::tcp::acceptor& acceptor, 
                  asio::io_context& io_context) {
    auto socket = std::make_shared<asio::ip::tcp::socket>(io_context);
    
    acceptor.async_accept(*socket, 
        [socket, &acceptor, &io_context](std::error_code ec) {
            if (!ec) {
                // 处理新连接
                handle_connection(socket);
            }
            
            // 继续接受下一个连接
            start_accept(acceptor, io_context);
        });
}
```

## 常见模式与最佳实践

### 1. 使用strand避免竞态条件
```cpp
asio::io_context io;
asio::io_context::strand strand(io);

// 使用strand确保回调顺序执行
timer1.async_wait(asio::bind_executor(strand, handler1));
timer2.async_wait(asio::bind_executor(strand, handler2));
```

### 2. 使用工作守卫防止io_context退出
```cpp
asio::io_context io;
asio::executor_work_guard<asio::io_context::executor_type> work = 
    asio::make_work_guard(io);

// io.run() 不会退出直到work被重置
std::thread io_thread([&io](){ io.run(); });

// 当不再需要时
work.reset();
io_thread.join();
```

### 3. 超时处理
```cpp
void connect_with_timeout(asio::io_context& io, asio::ip::tcp::socket& socket,
                         asio::ip::tcp::endpoint endpoint, std::chrono::seconds timeout) {
    // 设置连接超时
    asio::steady_timer timer(io, timeout);
    
    // 开始异步连接
    socket.async_connect(endpoint, [&](const std::error_code& ec) {
        if (timer.cancel()) {
            // 连接完成，定时器被取消
            if (!ec) {
                // 连接成功
            } else {
                // 连接错误
            }
        }
    });
    
    // 设置超时处理
    timer.async_wait([&](const std::error_code& ec) {
        if (!ec) {
            // 超时，取消连接
            socket.close();
        }
    });
}
```

## 注意事项与常见问题

1. **保持io_context运行**: 确保io_context.run()在所有异步操作完成前不退出
2. **生命周期管理**: 异步操作中使用的对象必须在操作完成前保持有效
3. **错误处理**: 始终检查错误码，尤其是在异步回调中
4. **不要阻塞事件循环**: 避免在异步回调中执行耗时操作
5. **线程安全**: 记住io_context不是线程安全的，使用strand保证回调安全

## 进阶主题

1. **SSL支持**: 使用asio::ssl进行加密通信
2. **HTTP客户端/服务器**: 结合Boost.Beast库可实现HTTP功能
3. **WebSocket**: 同样通过Boost.Beast支持
4. **异步文件操作**: 平台依赖性较高，但可封装为类似网络接口的形式
5. **自定义协议实现**: 利用Asio的异步模型实现自定义协议 