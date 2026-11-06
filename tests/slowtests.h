#pragma once

// Slow tests (performance on thousands of nodes) run only on explicit request, so the usual run stays
// short (user's request 2026-10-23): set MENTIS_SLOW_TESTS=1 to run them.

#include <QTest>

#define MENTIS_SLOW_TEST()                                                                   \
    do {                                                                                           \
        if (qEnvironmentVariableIsEmpty("MENTIS_SLOW_TESTS"))                                \
            QSKIP("Slow test: set MENTIS_SLOW_TESTS=1 to run it");                           \
    } while (0)
