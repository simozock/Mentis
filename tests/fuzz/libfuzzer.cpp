// libFuzzer driver (CI on Linux, clang -fsanitize=fuzzer,address,undefined): one executable per
// target, chosen by MENTIS_FUZZ_TARGET (markdown, richText, mapJson, document). See fuzztargets.h.
#include "fuzztargets.h"

#include <QApplication>
#include <QTemporaryDir>

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    static QApplication app(*argc, *argv);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const QByteArray input = QByteArray::fromRawData(reinterpret_cast<const char *>(data), qsizetype(size));
#if MENTIS_FUZZ_DOCUMENT
    static QTemporaryDir folder;
    FuzzTargets::document(input, folder.path());
#else
    FuzzTargets::MENTIS_FUZZ_TARGET(input);
#endif
    return 0;
}
