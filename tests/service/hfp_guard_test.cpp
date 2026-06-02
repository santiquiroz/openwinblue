// tests/service/hfp_guard_test.cpp
#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <combaseapi.h>

#include "hfp_guard.h"

TEST(HfpGuard, ConstructsWithoutCrash) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        owb::HfpGuard guard;
        EXPECT_FALSE(guard.is_active());
    }
    CoUninitialize();
}

TEST(HfpGuard, StartAndStopAreIdempotent) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        owb::HfpGuard guard;
        bool ok1 = guard.start();
        bool ok2 = guard.start(); // second start is a no-op
        EXPECT_EQ(ok1, ok2);      // both return the same result
        guard.stop();
        guard.stop();             // double stop must not crash
        EXPECT_FALSE(guard.is_active());
    }
    CoUninitialize();
}
