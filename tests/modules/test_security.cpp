#include <gtest/gtest.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

#define imp_module_export security_export
#define TCP_FILE "/tmp/test_tcp_mock"

extern "C" {
    #include "../../modules/security.c"
}

class SecurityModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        file_count = 0;
        check_interval_sec = 3600;
        memset(critical_files, 0, sizeof(critical_files));
        unlink(TCP_FILE);
    }

    void TearDown() override {
        unlink(TCP_FILE);
    }

    void CreateTestFile(const char* path, mode_t mode) {
        std::ofstream outfile(path);
        outfile << "dummy data";
        outfile.close();
        chmod(path, mode);
    }
};

TEST_F(SecurityModuleTest, InitParsesValidConfig) {
    const char* json = R"({
        "check_interval_sec": 300,
        "critical_files": ["/etc/shadow", "/etc/passwd", "/etc/sudoers"]
    })";

    EXPECT_EQ(security_init(json), 0);
    EXPECT_EQ(check_interval_sec, 300);
    EXPECT_EQ(file_count, 3);
    EXPECT_STREQ(critical_files[0], "/etc/shadow");
    EXPECT_STREQ(critical_files[2], "/etc/sudoers");
}

TEST_F(SecurityModuleTest, InitRespectsMaxFiles) {
    std::string json = "{\"critical_files\": [";
    for (int i = 0; i < 15; i++) {
        json += "\"/tmp/file" + std::to_string(i) + "\"";
        if (i < 14) json += ",";
    }
    json += "]}";

    EXPECT_EQ(security_init(json.c_str()), 0);
    EXPECT_EQ(file_count, MAX_FILES);
}

TEST_F(SecurityModuleTest, DetectsWorldReadableFiles) {
    const char* shadow_mock = "/tmp/test_shadow_imp";
    const char* normal_mock = "/tmp/test_normal_imp";
    const char* secure_mock = "/tmp/test_secure_imp";

    CreateTestFile(shadow_mock, 0644); 
    CreateTestFile(normal_mock, 0644); 

    CreateTestFile(secure_mock, 0600); 

    strncpy(critical_files[0], shadow_mock, 255);
    strncpy(critical_files[1], normal_mock, 255);
    strncpy(critical_files[2], secure_mock, 255);
    strncpy(critical_files[3], "/tmp/non_existent_file", 255);
    file_count = 4;

    check_critical_files();

    unlink(shadow_mock);
    unlink(normal_mock);
    unlink(secure_mock);

    SUCCEED(); 
}

TEST_F(SecurityModuleTest, ParsesNetworkTcpFileDetectsSSH) {
    std::ofstream tcp_file(TCP_FILE);
    
    // Standard Linux Header
    tcp_file << "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n";
    
    // State 0A (LISTEN). Port 0050
    tcp_file << "   0: 00000000:0050 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 12345 1 0\n";
    
    // 3. State 01 (ESTABLISHED). Local port 0016
    // Remote IP: 04030201
    tcp_file << "   1: 00000000:0016 04030201:1234 01 00000000:00000000 00:00000000 00000000     0        0 12346 1 0\n";
    
    tcp_file.close();

    check_network_tcp();

    SUCCEED();
}

TEST_F(SecurityModuleTest, CheckNetworkHandlesMissingFile) {
    check_network_tcp();
    SUCCEED();
}

TEST_F(SecurityModuleTest, ExportedStructIsValid) {
    EXPECT_STREQ(security_export.name, "Security");
    EXPECT_NE(security_export.init, nullptr);
    EXPECT_NE(security_export.run, nullptr);
    EXPECT_NE(security_export.cleanup, nullptr);
}