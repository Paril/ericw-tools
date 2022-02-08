#include "gtest/gtest.h"
#include "common/settings.hh"

// test booleans
TEST(settings, booleanFlagImplicit)
{
    settings::lockable_bool boolSetting("locked", false);
    settings::dict settings{ &boolSetting };
    const char *arguments[] = {
        "qbsp.exe",
        "-locked"
    };
    settings.parse(token_parser_t { std::size(arguments) - 1, arguments + 1 });
    ASSERT_EQ(boolSetting.boolValue(), true);
}

TEST(settings, booleanFlagExplicit)
{
    settings::lockable_bool boolSetting("locked", false);
    settings::dict settings{ &boolSetting };
    const char *arguments[] = {
        "qbsp.exe",
        "-locked",
        "1"
    };
    settings.parse(token_parser_t { std::size(arguments) - 1, arguments + 1 });
    ASSERT_EQ(boolSetting.boolValue(), true);
}

TEST(settings, booleanFlagStray)
{
    settings::lockable_bool boolSetting("locked", false);
    settings::dict settings{ &boolSetting };
    const char *arguments[] = {
        "qbsp.exe",
        "-locked",
        "stray"
    };
    settings.parse(token_parser_t { std::size(arguments) - 1, arguments + 1 });
    ASSERT_EQ(boolSetting.boolValue(), true);
}

// test scalars
TEST(settings, scalarSimple)
{
    settings::lockable_scalar scalarSetting("scale", 1.0);
    settings::dict settings{ &scalarSetting };
    const char *arguments[] = {
        "qbsp.exe",
        "-scale",
        "1.25"
    };
    settings.parse(token_parser_t { std::size(arguments) - 1, arguments + 1 });
    ASSERT_EQ(scalarSetting.floatValue(), 1.25);
}

TEST(settings, scalarNegative)
{
    settings::lockable_scalar scalarSetting("scale", 1.0);
    settings::dict settings{ &scalarSetting };
    const char *arguments[] = {
        "qbsp.exe",
        "-scale",
        "-0.25"
    };
    settings.parse(token_parser_t { std::size(arguments) - 1, arguments + 1 });
    ASSERT_EQ(scalarSetting.floatValue(), -0.25);
}

TEST(settings, scalarInfinity)
{
    settings::lockable_scalar scalarSetting("scale", 1.0);
    settings::dict settings{ &scalarSetting };
    const char *arguments[] = {
        "qbsp.exe",
        "-scale",
        "INFINITY"
    };
    settings.parse(token_parser_t { std::size(arguments) - 1, arguments + 1 });
    ASSERT_EQ(scalarSetting.floatValue(), std::numeric_limits<vec_t>::infinity());
}

TEST(settings, scalarNAN)
{
    settings::lockable_scalar scalarSetting("scale", 1.0);
    settings::dict settings{ &scalarSetting };
    const char *arguments[] = {
        "qbsp.exe",
        "-scale",
        "NAN"
    };
    settings.parse(token_parser_t { std::size(arguments) - 1, arguments + 1 });
    ASSERT_TRUE(std::isnan(scalarSetting.floatValue()));
}

TEST(settings, scalarScientific)
{
    settings::lockable_scalar scalarSetting("scale", 1.0);
    settings::dict settings{ &scalarSetting };
    const char *arguments[] = {
        "qbsp.exe",
        "-scale",
        "1.54334E-34"
    };
    settings.parse(token_parser_t { std::size(arguments) - 1, arguments + 1 });
    ASSERT_EQ(scalarSetting.floatValue(), 1.54334E-34);
}

TEST(settings, scalarEOF)
{
    settings::lockable_scalar scalarSetting("scale", 1.0);
    settings::dict settings{ &scalarSetting };
    const char *arguments[] = {
        "qbsp.exe",
        "-scale"
    };
    ASSERT_THROW(settings.parse(token_parser_t { std::size(arguments) - 1, arguments + 1 }), settings::parse_exception);
}

TEST(settings, scalarStray)
{
    settings::lockable_scalar scalarSetting("scale", 1.0);
    settings::dict settings{ &scalarSetting };
    const char *arguments[] = {
        "qbsp.exe",
        "-scale",
        "stray"
    };
    ASSERT_THROW(settings.parse(token_parser_t { std::size(arguments) - 1, arguments + 1 }), settings::parse_exception);
}

// test scalars
TEST(settings, vec3Simple)
{
    settings::lockable_vec3 scalarSetting("origin", 0, 0, 0);
    settings::dict settings{ &scalarSetting };
    const char *arguments[] = {
        "qbsp.exe",
        "-origin",
        "1",
        "2",
        "3"
    };
    settings.parse(token_parser_t { std::size(arguments) - 1, arguments + 1 });
    ASSERT_EQ(scalarSetting.vec3Value(), (qvec3d { 1, 2, 3 }));
}

TEST(settings, vec3Complex)
{
    settings::lockable_vec3 scalarSetting("origin", 0, 0, 0);
    settings::dict settings{ &scalarSetting };
    const char *arguments[] = {
        "qbsp.exe",
        "-origin",
        "-12.5",
        "-INFINITY",
        "NAN"
    };
    settings.parse(token_parser_t { std::size(arguments) - 1, arguments + 1 });
    ASSERT_EQ(scalarSetting.vec3Value()[0], -12.5);
    ASSERT_EQ(scalarSetting.vec3Value()[1], -std::numeric_limits<vec_t>::infinity());
    ASSERT_TRUE(std::isnan(scalarSetting.vec3Value()[2]));
}

TEST(settings, vec3Incomplete)
{
    settings::lockable_vec3 scalarSetting("origin", 0, 0, 0);
    settings::dict settings{ &scalarSetting };
    const char *arguments[] = {
        "qbsp.exe",
        "-origin",
        "1",
        "2"
    };
    ASSERT_THROW(settings.parse(token_parser_t { std::size(arguments) - 1, arguments + 1 }), settings::parse_exception);
}

TEST(settings, vec3Stray)
{
    settings::lockable_vec3 scalarSetting("origin", 0, 0, 0);
    settings::dict settings{ &scalarSetting };
    const char *arguments[] = {
        "qbsp.exe",
        "-origin",
        "1",
        "2",
        "abc"
    };
    ASSERT_THROW(settings.parse(token_parser_t { std::size(arguments) - 1, arguments + 1 }), settings::parse_exception);
}

// test string formatting
TEST(settings, stringSimple)
{
    settings::lockable_string stringSetting("name", "");
    settings::dict settings{ &stringSetting };
    const char *arguments[] = {
        "qbsp.exe",
        "-name",
        "i am a string with spaces in it"
    };
    settings.parse(token_parser_t { std::size(arguments) - 1, arguments + 1 });
    ASSERT_EQ(stringSetting.stringValue(), arguments[2]);
}

TEST(settings, stringSpan)
{
    settings::lockable_string stringSetting("name", "");
    settings::dict settings{ &stringSetting };
    const char *arguments[] = {
        "qbsp.exe",
        "-name",
        "i",
        "am",
        "a",
        "string"
    };
    settings.parse(token_parser_t { std::size(arguments) - 1, arguments + 1 });
    ASSERT_EQ(stringSetting.stringValue(), "i am a string");
}

TEST(settings, stringSpanWithBlockingOption)
{
    settings::lockable_string stringSetting("name", "");
    settings::lockable_bool flagSetting("flag", false);
    settings::dict settings{ &stringSetting, &flagSetting };
    const char *arguments[] = {
        "qbsp.exe",
        "-name",
        "i",
        "am",
        "a",
        "string",
        "-flag"
    };
    settings.parse(token_parser_t { std::size(arguments) - 1, arguments + 1 });
    ASSERT_EQ(stringSetting.stringValue(), "i am a string");
    ASSERT_EQ(flagSetting.boolValue(), true);
}

// test remainder
TEST(settings, remainder)
{
    settings::lockable_string stringSetting("name", "");
    settings::lockable_bool flagSetting("flag", false);
    settings::dict settings{ &stringSetting, &flagSetting };
    const char *arguments[] = {
        "qbsp.exe",
        "-name",
        "i",
        "am",
        "a",
        "string",
        "-flag",
        "remainder one",
        "remainder two"
    };
    auto remainder = settings.parse(token_parser_t { std::size(arguments) - 1, arguments + 1 });
    ASSERT_EQ(remainder[0], "remainder one");
    ASSERT_EQ(remainder[1], "remainder two");
}

// test double-hyphens
TEST(settings, doubleHyphen)
{
    settings::lockable_bool boolSetting("locked", false);
    settings::lockable_string stringSetting("name", "");
    settings::dict settings{&boolSetting, &stringSetting};
    const char *arguments[] = {"qbsp.exe", "--locked", "--name", "my name!"};
    settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1});
    ASSERT_EQ(boolSetting.boolValue(), true);
    ASSERT_EQ(stringSetting.stringValue(), "my name!");
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
