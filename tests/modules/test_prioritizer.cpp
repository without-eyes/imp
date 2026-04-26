#include <gtest/gtest.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define imp_module_export prioritizer_export

extern "C" {
    #include "../../modules/prioritizer.c"
}

class PrioritizerTest : public ::testing::Test {
protected:
    void SetUp() override {
        check_interval_sec = 10;
        cpu_threshold = 80;
        ram_threshold_kb = 1024 * 1024;
        important_count = 0;
        memset(important_procs, 0, sizeof(important_procs));
    }
    
    void TearDown() override {
    }
};

TEST_F(PrioritizerTest, InitParsesValidConfig) {
    const char* json = R"({
        "check_interval_sec": 5,
        "cpu_threshold_percent": 90,
        "ram_threshold_mb": 512,
        "important_processes": ["sshd", "systemd", "imp"]
    })";

    EXPECT_EQ(prioritizer_init(json), 0);
    
    EXPECT_EQ(check_interval_sec, 5);
    EXPECT_EQ(cpu_threshold, 90);
    EXPECT_EQ(ram_threshold_kb, 512 * 1024);
    EXPECT_EQ(important_count, 3);
    
    EXPECT_STREQ(important_procs[0], "sshd");
    EXPECT_STREQ(important_procs[1], "systemd");
    EXPECT_STREQ(important_procs[2], "imp");
}

TEST_F(PrioritizerTest, InitFailsOnInvalidJson) {
    EXPECT_EQ(prioritizer_init("{invalid_json: true"), -1);
}

TEST_F(PrioritizerTest, InitRespectsMaxImportantProcesses) {
    std::string json = "{\"important_processes\": [";
    for (int i = 0; i < 25; i++) {
        json += "\"proc" + std::to_string(i) + "\"";
        if (i < 24) json += ",";
    }
    json += "]}";

    EXPECT_EQ(prioritizer_init(json.c_str()), 0);
    EXPECT_EQ(important_count, MAX_IMPORTANT);
}

TEST_F(PrioritizerTest, IsImportantReturnsCorrectBoolean) {
    strcpy(important_procs[0], "Xorg");
    strcpy(important_procs[1], "bash");
    important_count = 2;

    EXPECT_EQ(is_important("Xorg"), 1);
    EXPECT_EQ(is_important("bash"), 1);
    EXPECT_EQ(is_important("firefox"), 0);
}

TEST_F(PrioritizerTest, LowersPriorityForHeavyProcess) {
    pid_t pid = fork();
    if (pid == 0) {
        ram_threshold_kb = 100 * 1024; 
        
        handle_heavy_process(getpid(), "memory_hog", 200 * 1024);

        int current_prio = getpriority(PRIO_PROCESS, getpid());
        
        if (current_prio == 19) {
            exit(0);
        } else {
            exit(1);
        }
    } else {
        int status;
        waitpid(pid, &status, 0);
        EXPECT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
}

TEST_F(PrioritizerTest, IgnoresLightProcess) {
    pid_t pid = fork();
    if (pid == 0) {
        int initial_prio = getpriority(PRIO_PROCESS, getpid());
        
        ram_threshold_kb = 100 * 1024; 
        handle_heavy_process(getpid(), "light_proc", 50 * 1024);

        int new_prio = getpriority(PRIO_PROCESS, getpid());
        
        if (initial_prio == new_prio) exit(0);
        else exit(1);
    } else {
        int status;
        waitpid(pid, &status, 0);
        EXPECT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
}

TEST_F(PrioritizerTest, ScanProcessesSmokeTest) {
    scan_processes();
    
    SUCCEED();
}

TEST_F(PrioritizerTest, ExportedStructIsValid) {
    EXPECT_STREQ(prioritizer_export.name, "Prioritizer");
    EXPECT_NE(prioritizer_export.init, nullptr);
    EXPECT_NE(prioritizer_export.run, nullptr);
    EXPECT_NE(prioritizer_export.cleanup, nullptr);
}