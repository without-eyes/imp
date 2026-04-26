#include <gtest/gtest.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <fstream>

#define CONFIG_PATH "/tmp/test_imp.json" 
#define DEFAULT_LOG_FILE "/tmp/test_imp.log"
#define DEFAULT_PID_FILE "/tmp/test_imp.pid"

extern "C" {
    #include "../../src/core/imp.c"
}

class ImpCoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        moduleCount = 0;
        daemonActive = 1;
        memset(registry, 0, sizeof(registry));
        
        strcpy(core_log_file, DEFAULT_LOG_FILE);
        strcpy(core_pid_file, DEFAULT_PID_FILE);
        
        std::ofstream outfile(CONFIG_PATH);
        outfile << "{\"modules\": []}";
        outfile.close();
        
        std::remove(DEFAULT_PID_FILE);
    }

    void TearDown() override {
        for (int i = 0; i < moduleCount; i++) {
            if (registry[i].configString) {
                free(registry[i].configString);
                registry[i].configString = nullptr;
            }
        }
        std::remove(CONFIG_PATH);
        std::remove(DEFAULT_PID_FILE);
    }
};

TEST_F(ImpCoreTest, ParsesCoreConfigCorrectly) {
    const char* json = R"({
        "log_level": "WARNING",
        "log_file": "/tmp/custom.log",
        "pid_file": "/tmp/custom.pid"
    })";

    int level = parse_core_config(json);
    
    EXPECT_EQ(level, LOG_LEVEL_WARNING);
    EXPECT_STREQ(core_log_file, "/tmp/custom.log");
    EXPECT_STREQ(core_pid_file, "/tmp/custom.pid");
    
    EXPECT_EQ(parse_core_config("{}"), LOG_LEVEL_INFO); 
    EXPECT_EQ(parse_core_config("{invalid json}"), LOG_LEVEL_INFO); 
}

TEST_F(ImpCoreTest, WritesAndRemovesPidFile) {
    strcpy(core_pid_file, "/tmp/test_imp.pid");
    
    write_pid_file();
    
    std::ifstream pid_stream("/tmp/test_imp.pid");
    ASSERT_TRUE(pid_stream.good());
    
    int written_pid;
    pid_stream >> written_pid;
    pid_stream.close();
    
    EXPECT_EQ(written_pid, getpid());
    
    remove_pid_file();
    std::ifstream pid_stream_after("/tmp/test_imp.pid");
    EXPECT_FALSE(pid_stream_after.good());
}

TEST_F(ImpCoreTest, HandleShutdownSetsFlag) {
    EXPECT_EQ(daemonActive, 1);
    handle_shutdown(SIGINT);
    EXPECT_EQ(daemonActive, 0);
}

TEST_F(ImpCoreTest, ReadConfigFailsOnMissingFile) {
    std::remove(CONFIG_PATH);
    char buffer[256];
    EXPECT_EQ(read_config(buffer, sizeof(buffer)), -1);
}

TEST_F(ImpCoreTest, ParseModulesCorrectly) {
    const char* json = R"({
        "modules": [
            {
                "name": "test_mod",
                "enabled": true,
                "so_path": "/tmp/test.so",
                "config": {"key": "val"}
            }
        ]
    })";

    cJSON *root = cJSON_Parse(json);
    ASSERT_NE(root, nullptr);

    int result = parse_and_load_modules(json);
    EXPECT_EQ(result, 0); 
    
    EXPECT_EQ(moduleCount, 1);
    EXPECT_STREQ(registry[0].name, "test_mod");
    EXPECT_STREQ(registry[0].soPath, "/tmp/test.so");
    EXPECT_TRUE(registry[0].enabled);
    
    cJSON_Delete(root);
}

TEST_F(ImpCoreTest, ParseDisabledModule) {
    const char* json = R"({
        "modules": [
            {
                "name": "disabled_mod",
                "enabled": false,
                "so_path": "/tmp/test.so",
                "config": {}
            }
        ]
    })";

    int result = parse_and_load_modules(json);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(moduleCount, 0); 
}

TEST_F(ImpCoreTest, TestCleanup) {
    moduleCount = 1;
    strcpy(registry[0].name, "dummy");
    registry[0].pid = 0; 
    registry[0].enabled = true;
    registry[0].configString = strdup("{\"test\":\"data\"}"); 

    EXPECT_EQ(imp_cleanup(), 0);
    EXPECT_EQ(registry[0].configString, nullptr); 
}

TEST_F(ImpCoreTest, TestDaemonize) {
    pid_t pid = fork();
    if (pid == 0) {
        imp_daemonize();
        exit(0);
    } else {
        int status;
        waitpid(pid, &status, 0);
        EXPECT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
}