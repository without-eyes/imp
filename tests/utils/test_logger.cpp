#include <gtest/gtest.h>
#include <fstream>
#include <string>

extern "C" {
    #include "../../src/utils/logger.c" 
}

class LoggerTest : public ::testing::Test {
protected:
    const char* test_log_path = "/tmp/test_imp.log";

    void SetUp() override {
        LoggerConfig* config = log_get_config();
        pthread_mutex_lock(&config->lock);
        config->isInitialized = false;
        if (config->fd) {
            fclose(config->fd);
            config->fd = NULL;
        }
        config->level = LOG_LEVEL_UNINITIALIZED;
        pthread_mutex_unlock(&config->lock);

        std::remove(test_log_path);
    }

    void TearDown() override {
        log_deinit();
        std::remove(test_log_path);
    }
};

TEST_F(LoggerTest, InitializesSuccessfully) {
    EXPECT_EQ(log_init(test_log_path, LOG_LEVEL_DEBUG), 0);
    EXPECT_TRUE(log_get_config()->isInitialized);
}

TEST_F(LoggerTest, WritesToLogFile) {
    log_init(test_log_path, LOG_LEVEL_DEBUG);
    LOG_INFO("TestModule", "Hello GoogleTest!");

    std::ifstream log_file(test_log_path);
    std::string line;
    std::getline(log_file, line);

    EXPECT_NE(line.find("Hello GoogleTest!"), std::string::npos);
    EXPECT_NE(line.find("INFO"), std::string::npos);
}

TEST_F(LoggerTest, RespectsLogLevel) {
    log_init(test_log_path, LOG_LEVEL_ERROR);
    LOG_INFO("TestModule", "This should NOT be written");
    
    std::ifstream log_file(test_log_path);
    std::string line;

    EXPECT_FALSE(std::getline(log_file, line));
}

TEST_F(LoggerTest, StaticLevelToString) {
    EXPECT_STREQ(log_level_to_string(LOG_LEVEL_CRITICAL), "CRITICAL");
    EXPECT_STREQ(log_level_to_string(LOG_LEVEL_DEBUG), "DEBUG");
    EXPECT_STREQ(log_level_to_string((log_level_t)99), "UNKNOWN");
}

TEST_F(LoggerTest, StaticTimeFormatting) {
    char buffer[TIME_STR_SIZE];
    log_get_curr_time(buffer, sizeof(buffer));
    
    EXPECT_TRUE(isdigit(buffer[0]));
    EXPECT_EQ(buffer[4], '-');
    EXPECT_EQ(buffer[7], '-');
    EXPECT_EQ(buffer[strlen(buffer) - 4], '.');
}

TEST_F(LoggerTest, HandlesInitFailure) {
    EXPECT_EQ(log_init("/non/existent/path/log.txt", LOG_LEVEL_DEBUG), -1);
}

TEST_F(LoggerTest, PreventsDoubleInit) {
    log_init(test_log_path, LOG_LEVEL_DEBUG);
    
    EXPECT_EQ(log_init(test_log_path, LOG_LEVEL_ERROR), 0);
    EXPECT_EQ(log_get_config()->level, LOG_LEVEL_DEBUG);
}