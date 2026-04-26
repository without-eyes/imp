#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <chrono>

extern "C" {
    #include "../../src/utils/ipc.c"
}

class IPCTest : public ::testing::Test {
protected:
    void SetUp() override {
        unlink(IMP_SOCKET_PATH);
    }

    void TearDown() override {
        unlink(IMP_SOCKET_PATH);
    }
};

TEST_F(IPCTest, ReturnsErrorWhenBrokerDown) {
    EXPECT_EQ(ipc_send_message("TestModule", "INFO", "Hello"), -1);
}

TEST_F(IPCTest, SendsMessageSuccessfully) {
    bool received = false;
    char buffer[1024] = {0};

    std::thread mock_server([&]() {
        int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, IMP_SOCKET_PATH, sizeof(addr.sun_path) - 1);
        
        bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
        listen(server_fd, 1);

        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd >= 0) {
            read(client_fd, buffer, sizeof(buffer) - 1);
            received = true;
            close(client_fd);
        }
        close(server_fd);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int result = ipc_send_message("Security", "WARNING", "Unauthorized access");

    mock_server.join();

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(received);
    EXPECT_NE(strstr(buffer, "\"source\":\"Security\""), nullptr);
    EXPECT_NE(strstr(buffer, "\"level\":\"WARNING\""), nullptr);
}