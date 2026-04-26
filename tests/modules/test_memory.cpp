#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>
#include <unistd.h>
#include <dirent.h>

#define imp_module_export memory_export

extern "C" {
    #include "../../modules/memory.c"
}

class MemoryModuleTest : public ::testing::Test {
protected:
    const char* test_dir = "/tmp/imp_test_mem";

    void SetUp() override {
        target_count = 0;
        check_interval_sec = DEFAULT_CHECK_INTERVAL_SEC_VALUE;
        critical_disk_usage_percent = DEFAULT_CRITICAL_DISK_USAGE_PERCENT;
        memset(targets, 0, sizeof(targets));

        mkdir(test_dir, 0777);
    }

    void TearDown() override {
        DIR* dir = opendir(test_dir);
        if (dir) {
            struct dirent* entry;
            char filepath[512];
            while ((entry = readdir(dir)) != NULL) {
                if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                    snprintf(filepath, sizeof(filepath), "%s/%s", test_dir, entry->d_name);
                    unlink(filepath);
                }
            }
            closedir(dir);
        }
        rmdir(test_dir);
    }

    void CreateTestFile(const char* filename, int age_days) {
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", test_dir, filename);
        
        std::ofstream(filepath).put('A');

        if (age_days > 0) {
            struct utimbuf new_times;
            time_t target_time = time(NULL) - (age_days * 24 * 3600) - 3600;
            new_times.actime = target_time;
            new_times.modtime = target_time;
            utime(filepath, &new_times);
        }
    }
};

TEST_F(MemoryModuleTest, InitParsesValidConfig) {
    const char* json = R"({
        "check_interval_sec": 120,
        "critical_disk_usage_percent": 85,
        "targets": [
            {"path": "/tmp/a", "mask": "*.log", "max_age_days": 5},
            {"path": "/var/log", "mask": "*.tmp", "max_age_days": 1}
        ]
    })";

    EXPECT_EQ(memory_init(json), 0);
    EXPECT_EQ(check_interval_sec, 120);
    EXPECT_EQ(critical_disk_usage_percent, 85);
    EXPECT_EQ(target_count, 2);
    EXPECT_STREQ(targets[0].path, "/tmp/a");
    EXPECT_STREQ(targets[1].mask, "*.tmp");
}

TEST_F(MemoryModuleTest, InitFailsOnInvalidJson) {
    EXPECT_EQ(memory_init("{invalid json"), -1);
}

TEST_F(MemoryModuleTest, InitRespectsMaxTargets) {
    std::string json = "{\"targets\": [";
    for (int i = 0; i < 15; i++) {
        json += "{\"path\": \"/tmp\", \"mask\": \"*\", \"max_age_days\": 1}";
        if (i < 14) json += ",";
    }
    json += "]}";

    EXPECT_EQ(memory_init(json.c_str()), 0);
    EXPECT_EQ(target_count, MAX_TARGETS);
}

TEST_F(MemoryModuleTest, DeletesOnlyOldFilesMatchingMask) {
    strncpy(targets[0].path, test_dir, sizeof(targets[0].path));
    strncpy(targets[0].mask, "*.log", sizeof(targets[0].mask));
    targets[0].max_age_days = 2;
    target_count = 1;

    CreateTestFile("old.log", 5);
    CreateTestFile("new.log", 0);
    CreateTestFile("old.txt", 5);

    clean_target(&targets[0]);

    char path_old_log[512], path_new_log[512], path_old_txt[512];
    snprintf(path_old_log, sizeof(path_old_log), "%s/old.log", test_dir);
    snprintf(path_new_log, sizeof(path_new_log), "%s/new.log", test_dir);
    snprintf(path_old_txt, sizeof(path_old_txt), "%s/old.txt", test_dir);

    EXPECT_NE(access(path_old_log, F_OK), 0);
    EXPECT_EQ(access(path_new_log, F_OK), 0);
    EXPECT_EQ(access(path_old_txt, F_OK), 0);
}

TEST_F(MemoryModuleTest, CheckDiskSpaceTriggersOnThreshold) {
    critical_disk_usage_percent = 0;

    check_disk_space("/tmp");
    
    critical_disk_usage_percent = 101;
    check_disk_space("/tmp");
}

TEST_F(MemoryModuleTest, ExportedStructIsValid) {
    EXPECT_STREQ(memory_export.name, "Memory");
    EXPECT_NE(memory_export.init, nullptr);
    EXPECT_NE(memory_export.run, nullptr);
    EXPECT_NE(memory_export.cleanup, nullptr);
}