#include <gtest/gtest.h>
#include <fstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

#undef LOG_FILE_PATH
#define LOG_FILE_PATH "/tmp/test_dashboard_logs.txt"

extern "C" {
    #include "../../src/cli/dashboard.c"
}

class DashboardTest : public ::testing::Test {
protected:
    void SetUp() override {
        dashboard_active = 1;
        log_count = 0;
        memset(recent_logs, 0, sizeof(recent_logs));
        
        std::ofstream outfile(LOG_FILE_PATH);
        outfile.close();
        
        unlink(DASHBOARD_SOCKET_PATH);
    }

    void TearDown() override {
        unlink(LOG_FILE_PATH);
        unlink(DASHBOARD_SOCKET_PATH);
    }
};

TEST_F(DashboardTest, ParseStandardLogLine) {
    const char* raw_line = "[2026-04-26 15:30:00.123][PID:1234][Security][security.c:45][CRITICAL] Unauthorized access";
    ParsedLog parsed;
    
    parse_log_line(raw_line, &parsed);
    
    EXPECT_EQ(parsed.is_valid, 1);
    EXPECT_STREQ(parsed.time, "15:30:00");
    EXPECT_STREQ(parsed.module, "Security");
    EXPECT_STREQ(parsed.level, "CRITICAL");
    EXPECT_STREQ(parsed.message, "Unauthorized access");
}

TEST_F(DashboardTest, ParseLiveIPCLogLine) {
    const char* raw_line = "[LIVE IPC] {\"source\":\"Memory\", \"level\":\"WARNING\", \"message\":\"Disk full\"}";
    ParsedLog parsed;
    
    parse_log_line(raw_line, &parsed);
    
    EXPECT_EQ(parsed.is_valid, 1);
    EXPECT_STREQ(parsed.time, "LIVE");
    EXPECT_STREQ(parsed.module, "Memory");
    EXPECT_STREQ(parsed.level, "WARNING");
    EXPECT_STREQ(parsed.message, "Disk full");
}

TEST_F(DashboardTest, ParseInvalidLogLine) {
    const char* raw_line = "Just some random garbage text";
    ParsedLog parsed;
    
    parse_log_line(raw_line, &parsed);
    EXPECT_EQ(parsed.is_valid, 0);
}

TEST_F(DashboardTest, SystemStatsSmokeTest) {
    double cpu = get_cpu_usage();
    double ram = get_ram_usage();
    double swap = get_swap_usage();
    double temp = get_cpu_temp();
    
    EXPECT_GE(cpu, 0.0);
    EXPECT_GE(ram, 0.0);
    EXPECT_GE(swap, 0.0);
    EXPECT_GE(temp, -1.0);
    SUCCEED();
}

TEST_F(DashboardTest, PrintBarOutputsCorrectly) {
    testing::internal::CaptureStdout();
    
    print_bar("TestCPU", 50.5, "\033[32m", "(45.0°C)");
    
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_NE(output.find("TestCPU"), std::string::npos);
    EXPECT_NE(output.find("50.5%"), std::string::npos);
    EXPECT_NE(output.find("45.0°C"), std::string::npos);
}

TEST_F(DashboardTest, LoadInitialLogsWorks) {
    std::ofstream outfile(LOG_FILE_PATH);
    outfile << "[2026-04-26 10:00:00.000][PID:1][Core][imp.c:10][INFO] Started\n";
    outfile.close();

    load_initial_logs();
    
    EXPECT_EQ(log_count, 1);
    EXPECT_NE(strstr(recent_logs[0], "Started"), nullptr);
}

TEST_F(DashboardTest, InteractiveDashboardLoopAndIPC) {
    pid_t pid = fork();
    
    if (pid == 0) {
        freopen("/dev/null", "w", stdout); 
        run_interactive_dashboard();
        exit(0);
    } else {
        usleep(100000); 

        int sock = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, DASHBOARD_SOCKET_PATH, sizeof(addr.sun_path) - 1);
        
        int connected = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
        EXPECT_EQ(connected, 0);
        
        if (connected == 0) {
            const char* test_msg = "{\"source\":\"TEST\", \"level\":\"INFO\", \"message\":\"Ping\"}";
            write(sock, test_msg, strlen(test_msg));
            close(sock);
        }

        usleep(50000);
        
        kill(pid, SIGINT);
        
        int status;
        waitpid(pid, &status, 0);
        
        EXPECT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
}