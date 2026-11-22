// SPDX-License-Identifier: MIT
// Minimal test harness (no external dependency).
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace test {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& All() {
    static std::vector<Case> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { All().push_back({name, std::move(fn)}); }
};

struct Failure {
    std::string message;
};

inline int& FailureCount() {
    static int count = 0;
    return count;
}

inline const char*& CurrentTest() {
    static const char* name = "";
    return name;
}

inline void Fail(const char* file, int line, const std::string& message) {
    ++FailureCount();
    std::fprintf(stderr, "  FAIL %s:%d  [%s]\n    %s\n", file, line, CurrentTest(), message.c_str());
}

} // namespace test

#define TEST_CASE_NAME2(a, b) a##b
#define TEST_CASE_NAME(a, b) TEST_CASE_NAME2(a, b)
#define TEST(name)                                                                              \
    static void TEST_CASE_NAME(test_fn_, __LINE__)();                                           \
    static ::test::Registrar TEST_CASE_NAME(test_reg_, __LINE__)(name,                          \
                                                                &TEST_CASE_NAME(test_fn_, __LINE__)); \
    static void TEST_CASE_NAME(test_fn_, __LINE__)()

#define CHECK(cond)                                                                             \
    do {                                                                                        \
        if (!(cond)) ::test::Fail(__FILE__, __LINE__, #cond);                                   \
    } while (0)

#define CHECK_EQ(a, b)                                                                          \
    do {                                                                                        \
        const auto va = (a);                                                                    \
        const auto vb = (b);                                                                    \
        if (!(va == vb))                                                                        \
            ::test::Fail(__FILE__, __LINE__,                                                    \
                         std::string(#a) + " == " + #b + " (got " + ::test::ToStr(va) +         \
                             " vs " + ::test::ToStr(vb) + ")");                                 \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                   \
    do {                                                                                        \
        const double va = static_cast<double>(a);                                               \
        const double vb = static_cast<double>(b);                                               \
        const double d = va > vb ? va - vb : vb - va;                                           \
        if (d > (eps))                                                                          \
            ::test::Fail(__FILE__, __LINE__,                                                    \
                         std::string(#a) + " ~= " + #b + " (got " + std::to_string(va) +        \
                             " vs " + std::to_string(vb) + ")");                                \
    } while (0)

namespace test {

inline std::string ToStr(const std::string& v) { return v; }
inline std::string ToStr(const char* v) { return v ? v : "(null)"; }
inline std::string ToStr(bool v) { return v ? "true" : "false"; }
inline std::string ToStr(int v) { return std::to_string(v); }
inline std::string ToStr(unsigned v) { return std::to_string(v); }
inline std::string ToStr(long v) { return std::to_string(v); }
inline std::string ToStr(size_t v) { return std::to_string(v); }
inline std::string ToStr(double v) { return std::to_string(v); }
inline std::string ToStr(float v) { return std::to_string(v); }
inline std::string ToStr(const char16_t*) { return "(u16)"; }

int RunAll(const char* filter);
std::string Narrow(const std::wstring& w);

} // namespace test
