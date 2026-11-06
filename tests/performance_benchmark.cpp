#include "bmapstorage.h"
#include "mindmapmodel.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>
#include <functional>

namespace {
qint64 elapsedMs(const std::function<void()> &operation) {
    QElapsedTimer timer;
    timer.start();
    operation();
    return timer.elapsed();
}

QJsonObject runScenario(int nodeCount, const QString &directoryPath) {
    MindMapModel model;
    qint64 buildMs = elapsedMs([&] {
        for (int i = 1; i < nodeCount; ++i) {
            MindNode *parent = model.root();
            if (!parent->children.empty() && i % 4 != 0)
                parent = parent->children[static_cast<size_t>((i / 4) % parent->children.size())].get();
            model.addChild(parent, QStringLiteral("Nodo %1 con testo di benchmark").arg(i));
        }
    });

    BmapStorage::FlatMap snapshot;
    const qint64 snapshotMs = elapsedMs([&] { snapshot = model.toFlatSnapshot(); });

    const QString path = directoryPath + QStringLiteral("/benchmark-%1.mentis").arg(nodeCount);
    QString error;
    bool saved = false;
    const qint64 saveMs = elapsedMs([&] {
        saved = BmapStorage::save(path, model, BmapStorage::SaveReason::Automatic, &error);
    });

    MindMapModel restored;
    bool loaded = false;
    const qint64 loadMs = elapsedMs([&] { loaded = BmapStorage::load(path, restored, &error, false); });

    return {{QStringLiteral("nodes"), nodeCount},
            {QStringLiteral("build_ms"), buildMs},
            {QStringLiteral("snapshot_ms"), snapshotMs},
            {QStringLiteral("save_ms"), saveMs},
            {QStringLiteral("load_ms"), loadMs},
            {QStringLiteral("file_bytes"), QFileInfo(path).size()},
            {QStringLiteral("snapshot_entries"), snapshot.size()},
            {QStringLiteral("ok"), saved && loaded},
            {QStringLiteral("error"), error}};
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;

    QList<int> sizes;
    QString outputPath;
    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument == QStringLiteral("--output") && i + 1 < argc) {
            outputPath = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        bool ok = false;
        const int size = argument.toInt(&ok);
        if (ok && size > 0) sizes.append(size);
    }
    if (sizes.isEmpty()) sizes = {1000, 10000};

    bool success = true;
    QByteArray report;
    for (const int size : sizes) {
        const QJsonObject result = runScenario(size, directory.path());
        const QByteArray line = QJsonDocument(result).toJson(QJsonDocument::Compact);
        report += line + '\n';
        std::printf("%s\n", line.constData());
        success = success && result.value(QStringLiteral("ok")).toBool();
    }
    if (!outputPath.isEmpty()) {
        QFile output(outputPath);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate) || output.write(report) != report.size()) return 3;
    }
    return success ? 0 : 2;
}
