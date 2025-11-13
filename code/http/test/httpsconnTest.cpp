#include <iostream>
#include <thread>
#include <chrono>

#include "../httpsconn.h"
#include "../sslContext.h"

class HttpsConnTest {
public:
    void runAllTests() {
        std::cout << "=== HttpsConn Test Suite ===\n" << std::endl;
        
        TestConnectionPerformance();
        TestHttpsConnInitialization();
        TestHttpsConnInheritance();
        TestSSLInitialization();
        TestReadWriteWithoutSSL();
        TestErrorConditions();
        TestConnectionPerformance();
        
        std::cout << "\n=== All Tests Completed ===" << std::endl;
    }

    void TestHttpsConnInitialization() {
        std::cout << "=== Testing HttpsConn Initialization ===" << std::endl;
        
        HttpsConn conn;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(8443);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        
        conn.Init(123, addr);
        
        // test basic properties
        assert(conn.GetFd() == 123);
        assert(conn.GetPort() == 8443);
        assert(std::string(conn.GetIP()) == "127.0.0.1");
        assert(!conn.isHandShakeDone());
        
        std::cout << "✓ HttpsConn initialization test passed" << std::endl;
    }

    void TestHttpsConnInheritance() {
        std::cout << "=== Testing HttpsConn Inheritance ===" << std::endl;
        
        HttpsConn https_conn;
        HttpConn* base_conn = &https_conn;
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(8443);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        
        base_conn->Init(456, addr);
        
        // test multiple inheritance
        assert(base_conn->GetFd() == 456);
        assert(base_conn->GetPort() == 8443);
        
        std::cout << "✓ HttpsConn inheritance test passed" << std::endl;
    }

    void TestSSLInitialization() {
        std::cout << "=== Testing SSL Initialization ===" << std::endl;
        
        int sockfds[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockfds) != 0) {
            std::cerr << "✗ Failed to create socket pair" << std::endl;
            return;
        }
        
        int client_fd = sockfds[0];
        int server_fd = sockfds[1];
        
        // 初始化 SSL 上下文（需要提前准备测试证书）
        // SSLContext::GetInstance().Initialize("test_cert.pem", "test_key.pem");
        
        HttpsConn server_conn;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(8443);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        
        server_conn.Init(server_fd, addr);
        
        // test InitSSL
        bool ssl_init_result = server_conn.InitSSL();
        if (ssl_init_result) {
            std::cout << "✓ SSL initialization successful" << std::endl;
        } else {
            std::cout << "✗ SSL initialization failed (may need test certificates)" << std::endl;
        }
        
        close(client_fd);
        close(server_fd);
    }

    void TestReadWriteWithoutSSL() {
        std::cout << "=== Testing Read/Write Without SSL ===" << std::endl;
        
        int sockfds[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockfds) != 0) {
            std::cerr << "✗ Failed to create socket pair" << std::endl;
            return;
        }
        
        int client_fd = sockfds[0];
        int server_fd = sockfds[1];
        
        HttpsConn conn;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(8443);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        conn.Init(server_fd, addr);
        
        // 测试读取（没有数据应该返回错误）
        int saveErrno = 0;
        ssize_t read_result = conn.read(&saveErrno);
        
        if (read_result <= 0) {
            std::cout << "✓ Read on empty socket returned expected result: " << read_result << std::endl;
        } else {
            std::cout << "✗ Read on empty socket returned unexpected result: " << read_result << std::endl;
        }
        
        // 测试写入（没有数据应该返回 0）
        ssize_t write_result = conn.write(&saveErrno);
        if (write_result == 0) {
            std::cout << "✓ Write with no data returned 0" << std::endl;
        } else {
            std::cout << "✗ Write with no data returned: " << write_result << std::endl;
        }
        
        close(client_fd);
        close(server_fd);
    }

    void TestErrorConditions() {
        std::cout << "=== Testing Error Conditions ===" << std::endl;
        
        HttpsConn conn;
        
        // 测试未初始化的读取
        int saveErrno = 0;
        ssize_t result = conn.read(&saveErrno);
        
        if (result < 0 && saveErrno == EBADF) {
            std::cout << "✓ Read on uninitialized conn returned EBADF" << std::endl;
        } else {
            std::cout << "✗ Read on uninitialized conn returned: " << result 
                    << ", errno: " << saveErrno << std::endl;
        }
        
        // 测试未初始化的写入
        saveErrno = 0;
        result = conn.write(&saveErrno);
        
        if (result < 0 && saveErrno == EBADF) {
            std::cout << "✓ Write on uninitialized conn returned EBADF" << std::endl;
        } else {
            std::cout << "✗ Write on uninitialized conn returned: " << result 
                    << ", errno: " << saveErrno << std::endl;
        }
        
        // 测试关闭功能
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(8443);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        conn.Init(999, addr);
        
        assert(conn.GetFd() == 999);
        conn.Close();
        
        // 关闭后应该不能再操作
        saveErrno = 0;
        result = conn.read(&saveErrno);
        if (result < 0) {
            std::cout << "✓ Read after close returned error" << std::endl;
        } else {
            std::cout << "✗ Read after close returned: " << result << std::endl;
        }
    }

    void TestHttpOverHttpsSimulation() {
        std::cout << "=== Testing HTTP over HTTPS Simulation ===" << std::endl;
        
        int sockfds[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockfds) != 0) {
            std::cerr << "✗ Failed to create socket pair" << std::endl;
            return;
        }
        
        int client_fd = sockfds[0];
        int server_fd = sockfds[1];
        
        HttpsConn server_conn;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(8443);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        server_conn.Init(server_fd, addr);
        
        // 模拟客户端发送 HTTP 请求
        std::thread client_thread([client_fd]() {
            const char* http_request = 
                "GET /test HTTP/1.1\r\n"
                "Host: localhost:8443\r\n"
                "\r\n";
            
            write(client_fd, http_request, strlen(http_request));
            
            // 读取响应
            char buffer[1024];
            ssize_t len = read(client_fd, buffer, sizeof(buffer) - 1);
            if (len > 0) {
                buffer[len] = '\0';
                std::cout << "Client received: " << len << " bytes" << std::endl;
            }
        });
        
        // 服务器端处理
        int saveErrno = 0;
        
        // 读取请求
        ssize_t read_len = server_conn.read(&saveErrno);
        std::cout << "Server read: " << read_len << " bytes, errno: " << saveErrno << std::endl;
        
        if (read_len > 0) {
            // 处理请求
            bool process_result = server_conn.process();
            std::cout << "Process result: " << (process_result ? "true" : "false") << std::endl;
            
            // 发送响应
            ssize_t write_len = server_conn.write(&saveErrno);
            std::cout << "Server wrote: " << write_len << " bytes, errno: " << saveErrno << std::endl;
        }
        
        client_thread.join();
        
        close(client_fd);
        close(server_fd);
        std::cout << "✓ HTTP over HTTPS simulation completed" << std::endl;
    }

    void TestConnectionPerformance() {
        std::cout << "=== Testing Connection Performance ===" << std::endl;
        
        const int NUM_CONNECTIONS = 100;
        std::vector<std::unique_ptr<HttpsConn>> connections;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // 创建多个连接测试性能
        for (int i = 0; i < NUM_CONNECTIONS; ++i) {
            auto conn = std::make_unique<HttpsConn>();
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(8443 + i);
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
            
            conn->Init(1000 + i, addr);
            connections.push_back(std::move(conn));
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time);
        
        std::cout << "Created " << NUM_CONNECTIONS << " HttpsConn instances in " 
                << duration.count() << " microseconds" << std::endl;
        std::cout << "Average time per connection: " 
                << duration.count() / NUM_CONNECTIONS << " microseconds" << std::endl;
        
        // 清理
        for (auto& conn : connections) {
            conn->Close();
        }
    }

}; 

int main() {
    HttpsConnTest tester;
    tester.runAllTests();

    return 0;
}