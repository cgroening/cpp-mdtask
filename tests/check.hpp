#pragma once

#include <cstdio>
#include <print>
#include <string_view>

// A minimal, dependency-free assertion harness. Counts checks and failures
// in shared inline globals so every test translation unit reports into the
// same totals; the test runner ends by calling summary().
namespace check {

inline int failures = 0;
inline int checks   = 0;

/** Records one check result and prints failing expressions to stderr. */
inline void report(
    bool passed, std::string_view expression, std::string_view file, int line
) {
    ++checks;
    if(!passed) {
        ++failures;
        std::println(stderr, "FAIL {}:{}  {}", file, line, expression);
    }
}

/**
 * Prints the final result line for the whole suite.
 *
 * @param suite Display name of the test suite.
 * @return Process exit code: 0 when everything passed, 1 otherwise.
 */
inline int summary(std::string_view suite) {
    if(failures == 0) {
        std::println("ok  {} - {} checks passed", suite, checks);
        return 0;
    }
    std::println(
        stderr, "FAILED {} - {}/{} checks failed", suite, failures, checks
    );
    return 1;
}

}  // namespace check

#define CHECK(expr) ::check::report((expr), #expr, __FILE__, __LINE__)
