#include "logging.h"
#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdio>
#include <fstream>

TEST(LoggingTest, Disabled) {
    toolbelt::Logger logger("foobar", false);
    logger.log(toolbelt::LogLevel::kINFO, "this should not be logged");
}

TEST(LoggingTest, Levels) {
    toolbelt::Logger logger;

    struct Level {
        std::string name;
        toolbelt::LogLevel level;
    } levels[] = {
            {"verbose", toolbelt::LogLevel::kVERBOSE_DEBUG},
            {"debug", toolbelt::LogLevel::kDBG},
            {"info", toolbelt::LogLevel::kINFO},
            {"warning", toolbelt::LogLevel::kWARNING},
            {"error", toolbelt::LogLevel::kERROR},
            {"fatal", toolbelt::LogLevel::FATAL},
    };

    for (auto& level : levels) {
        logger.setLogLevel(level.name);
        ASSERT_EQ(level.level, logger.getLogLevel());
    }
    logger.setLogLevel(toolbelt::LogLevel::kINFO);

    logger.log(toolbelt::LogLevel::kVERBOSE_DEBUG, "verbose debug");
    logger.log(toolbelt::LogLevel::kDBG, "debug");
    logger.log(toolbelt::LogLevel::kINFO, "info");
    logger.log(toolbelt::LogLevel::kWARNING, "warning");
    logger.log(toolbelt::LogLevel::kERROR, "error");

    EXPECT_DEATH(logger.log(toolbelt::LogLevel::FATAL, "fatal"), "fatal");
}

static void ReadAndCheck(FILE* fp, const char* s) {
    char buf[256] = {};
    fgets(buf, sizeof(buf), fp);
    ASSERT_NE(nullptr, strstr(buf, s));
}

TEST(LoggingTest, Output) {
    toolbelt::Logger logger;

    int pipes[2];
    (void)pipe(pipes);

    FILE* logger_fp = fdopen(pipes[1], "w");
    FILE* read_fp = fdopen(pipes[0], "r");
    ASSERT_NE(nullptr, logger_fp);
    ASSERT_NE(nullptr, read_fp);

    logger.setOutputStream(logger_fp);

    toolbelt::LogLevel levels[] = {
            toolbelt::LogLevel::kVERBOSE_DEBUG,
            toolbelt::LogLevel::kDBG,
            toolbelt::LogLevel::kINFO,
            toolbelt::LogLevel::kWARNING,
            toolbelt::LogLevel::kERROR,
    };

    for (auto& min_level : levels) {
        logger.setLogLevel(min_level);
        for (auto& level : levels) {
            logger.log(level, "foobar");
            fflush(logger_fp);
            if (level >= min_level) {
                ReadAndCheck(read_fp, "foobar");
            }
        }
    }

    fclose(logger_fp);
    fclose(read_fp);
}

TEST(LoggingTest, Plain) {
    toolbelt::Logger defaultLogger("default", true, toolbelt::LogTheme::DEFAULT);
    toolbelt::Logger lightLogger("light", true, toolbelt::LogTheme::LIGHT);
    toolbelt::Logger darkLogger("dark", true, toolbelt::LogTheme::DARK);

    defaultLogger.setDisplayMode(toolbelt::LogDisplayMode::PLAIN);
    lightLogger.setDisplayMode(toolbelt::LogDisplayMode::PLAIN);
    darkLogger.setDisplayMode(toolbelt::LogDisplayMode::PLAIN);

    toolbelt::LogLevel levels[] = {
            toolbelt::LogLevel::kVERBOSE_DEBUG,
            toolbelt::LogLevel::kDBG,
            toolbelt::LogLevel::kINFO,
            toolbelt::LogLevel::kWARNING,
            toolbelt::LogLevel::kERROR,
    };

    std::string message = "test ";
    for (int i = 0; i < 100; i++) {
        for (auto& level : levels) {
            defaultLogger.setLogLevel(level);
            lightLogger.setLogLevel(level);
            darkLogger.setLogLevel(level);

            defaultLogger.log(level, message);
            lightLogger.log(level, message);
            darkLogger.log(level, message);
        }
        message += "test ";
    }
}

TEST(LoggingTest, Color) {
    toolbelt::Logger defaultLogger("default", true, toolbelt::LogTheme::DEFAULT);
    toolbelt::Logger lightLogger("light", true, toolbelt::LogTheme::LIGHT);
    toolbelt::Logger darkLogger("dark", true, toolbelt::LogTheme::DARK);

    defaultLogger.setDisplayMode(toolbelt::LogDisplayMode::COLOR);
    lightLogger.setDisplayMode(toolbelt::LogDisplayMode::COLOR);
    darkLogger.setDisplayMode(toolbelt::LogDisplayMode::COLOR);


    toolbelt::LogLevel levels[] = {
            toolbelt::LogLevel::kVERBOSE_DEBUG,
            toolbelt::LogLevel::kDBG,
            toolbelt::LogLevel::kINFO,
            toolbelt::LogLevel::kWARNING,
            toolbelt::LogLevel::kERROR,
    };

    std::string message = "test ";
    for (int i = 0; i < 100; i++) {
        for (auto& level : levels) {
            defaultLogger.setLogLevel(level);
            lightLogger.setLogLevel(level);
            darkLogger.setLogLevel(level);

            defaultLogger.log(level, message);
            lightLogger.log(level, message);
            darkLogger.log(level, message);
        }
        message += "test ";
    }
}

TEST(LoggingTest, Columnar) {
    toolbelt::Logger defaultLogger("default", true, toolbelt::LogTheme::DEFAULT);
    toolbelt::Logger lightLogger("light", true, toolbelt::LogTheme::LIGHT);
    toolbelt::Logger darkLogger("dark", true, toolbelt::LogTheme::DARK);

    defaultLogger.setDisplayMode(toolbelt::LogDisplayMode::COLUMNAR, 120);
    lightLogger.setDisplayMode(toolbelt::LogDisplayMode::COLUMNAR, 120);
    darkLogger.setDisplayMode(toolbelt::LogDisplayMode::COLUMNAR, 120);

    toolbelt::LogLevel levels[] = {
            toolbelt::LogLevel::kVERBOSE_DEBUG,
            toolbelt::LogLevel::kDBG,
            toolbelt::LogLevel::kINFO,
            toolbelt::LogLevel::kWARNING,
            toolbelt::LogLevel::kERROR,
    };

    std::string message = "test ";
    for (int i = 0; i < 100; i++) {
        for (auto& level : levels) {
            defaultLogger.setLogLevel(level);
            lightLogger.setLogLevel(level);
            darkLogger.setLogLevel(level);

            defaultLogger.log(level, message);
            lightLogger.log(level, message);
            darkLogger.log(level, message);
        }
        message += "test ";
    }
}

TEST(LoggingTest, ColumnarSplit) {
    toolbelt::Logger defaultLogger("default", true, toolbelt::LogTheme::DEFAULT);
    defaultLogger.setDisplayMode(toolbelt::LogDisplayMode::COLUMNAR, 60);
    defaultLogger.log(
            toolbelt::LogLevel::kINFO,
            "now is the time for all good men to come to the aid of the party\n");
    defaultLogger.log(
            toolbelt::LogLevel::kINFO, "the quick brown fox\njumps over the\nlazy dog");
}


TEST(LoggingTest, ColumnarLongSubsystem) {
    toolbelt::Logger longLogger(
            "this_is_a_very_long_subsystem_name_just_to_get_coverage_of_a_line",
            true,
            toolbelt::LogTheme::DEFAULT);
    longLogger.setDisplayMode(toolbelt::LogDisplayMode::COLUMNAR, 60);
    longLogger.log(
            toolbelt::LogLevel::kINFO,
            "now is the time for all good men to come to the aid of the party\n");
    longLogger.log(
            toolbelt::LogLevel::kINFO, "the quick brown fox\njumps over the\nlazy dog");
}

TEST(LoggingTest, Tee) {
    toolbelt::Logger logger("tee_test", true);
    logger.setDisplayMode(toolbelt::LogDisplayMode::COLOR);

    const std::string teeFile = "/tmp/foo/bar/tee_test.log";
    auto status = logger.setTeeFile(teeFile);
    ASSERT_TRUE(status.ok()) << status.toString();

    logger.log(toolbelt::LogLevel::kINFO, "this is a test message");

    std::ifstream teeStream(teeFile);
    ASSERT_TRUE(teeStream.is_open());
    std::string line;
    std::getline(teeStream, line);
    ASSERT_NE(line.find("this is a test message"), std::string::npos);
    teeStream.close();

    std::string tee2 = "/tmp/bar/foo/tee_test2.log";
    // Use another tee file.  This will close the previous one.
    status = logger.setTeeFile(tee2);
    ASSERT_TRUE(status.ok()) << status.toString();
    logger.log(toolbelt::LogLevel::kINFO, "this is another test message");
    std::ifstream teeStream2(tee2);
    ASSERT_TRUE(teeStream2.is_open());
    std::getline(teeStream2, line);
    ASSERT_NE(line.find("this is another test message"), std::string::npos);
    teeStream2.close();

    // Clean up
    remove(teeFile.c_str());
    remove(tee2.c_str());
}
