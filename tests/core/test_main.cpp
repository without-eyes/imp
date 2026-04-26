#include <gtest/gtest.h>
#include <string>

extern "C" {
    int daemon_calls = 0;
    int run_calls = 0;
    int cleanup_calls = 0;
    int ui_calls = 0;

    void mock_imp_daemonize(void) { daemon_calls++; }
    int mock_imp_run(void) { run_calls++; return 0; }
    int mock_imp_cleanup(void) { cleanup_calls++; return 0; }
    void mock_run_interactive_dashboard(void) { ui_calls++; }

    #define imp_daemonize mock_imp_daemonize
    #define imp_run mock_imp_run
    #define imp_cleanup mock_imp_cleanup
    #define run_interactive_dashboard mock_run_interactive_dashboard

    #define main imp_main
    #include "../../src/core/main.c"
    #undef main

    #undef imp_daemonize
    #undef imp_run
    #undef imp_cleanup
    #undef run_interactive_dashboard
}

class MainTest : public ::testing::Test {
protected:
    void SetUp() override {
        optind = 1; 
        daemon_calls = 0;
        run_calls = 0;
        cleanup_calls = 0;
        ui_calls = 0;
    }
};

TEST_F(MainTest, NoArgumentsPrintsHelp) {
    char* argv[] = {(char*)"imp", nullptr};
    
    testing::internal::CaptureStdout();
    int res = imp_main(1, argv);
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_EQ(res, 0);
    EXPECT_NE(output.find("Usage: imp [OPTIONS]"), std::string::npos);
}

TEST_F(MainTest, HelpFlagPrintsHelp) {
    char* argv[] = {(char*)"imp", (char*)"-h", nullptr};
    
    testing::internal::CaptureStdout();
    int res = imp_main(2, argv);
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_EQ(res, 0);
    EXPECT_NE(output.find("Options:"), std::string::npos);
}

TEST_F(MainTest, VersionFlagPrintsVersion) {
    char* argv[] = {(char*)"imp", (char*)"-v", nullptr};
    
    testing::internal::CaptureStdout();
    int res = imp_main(2, argv);
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_EQ(res, 0);
    EXPECT_NE(output.find(IMP_VERSION), std::string::npos);
}

TEST_F(MainTest, InvalidFlagPrintsHelpAndReturnsError) {
    char* argv[] = {(char*)"imp", (char*)"-x", nullptr};
    
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    
    int res = imp_main(2, argv);
    
    testing::internal::GetCapturedStdout();
    testing::internal::GetCapturedStderr();
    
    EXPECT_EQ(res, 1);
}

TEST_F(MainTest, DaemonFlagTriggersDaemonFlow) {
    char* argv[] = {(char*)"imp", (char*)"-d", nullptr};
    
    int res = imp_main(2, argv);
    
    EXPECT_EQ(res, 0);
    EXPECT_EQ(daemon_calls, 1);
    EXPECT_EQ(run_calls, 1);
    EXPECT_EQ(cleanup_calls, 1);
    EXPECT_EQ(ui_calls, 0);
}

TEST_F(MainTest, InteractiveFlagTriggersUI) {
    char* argv[] = {(char*)"imp", (char*)"-i", nullptr};
    
    int res = imp_main(2, argv);
    
    EXPECT_EQ(res, 0);
    EXPECT_EQ(ui_calls, 1);
    EXPECT_EQ(daemon_calls, 0);
    EXPECT_EQ(run_calls, 0);
}

TEST_F(MainTest, PreventsRunningBothModes) {
    char* argv[] = {(char*)"imp", (char*)"-d", (char*)"-i", nullptr};
    
    testing::internal::CaptureStdout();
    int res = imp_main(3, argv);
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_EQ(res, 1);
    EXPECT_NE(output.find("Cannot run both"), std::string::npos);

    EXPECT_EQ(ui_calls, 0);
    EXPECT_EQ(daemon_calls, 0);
}