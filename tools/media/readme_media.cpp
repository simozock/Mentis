// Dev tool (MENTIS_BUILD_MEDIA): renders the screenshots and animated GIFs of the README (media/) and
// of the user manual (manual/images/) from the real MainWindow on the offscreen platform. Maps are built with the agents' tools (McpMapTools), so the
// content goes through the same code as a user's edits. Settings, caches and files live in an
// isolated organization and in test mode: the user's own settings are never read or written.
//
// Usage: QT_QPA_PLATFORM=offscreen MentisReadmeMedia <repository folder> [only]
//   only = a comma-separated list of: map,outliner,gantt,pitch,home,dark,build,structures,themes,
//          window,updates,basics,styling,contents,outliner-pages,gantt-pages,pitch-pages,files,faster,
//          settings,help
#include "document.h"
#include "mainwindow.h"
#include "mapstructure.h"
#include "mapvariants.h"
#include "mcpmaptools.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "nodestyle.h"
#include "performance.h"
#include "pitch.h"
#include "pitchpage.h"
#include "pitchtemplate.h"
#include "pitchwindow.h"
#include "ganttplan.h"
#include "gantttaskpanel.h"
#include "ganttview.h"
#include "iconlibrary.h"
#include "manualwindow.h"
#include "noteeditor.h"
#include "resourcesdialog.h"
#include "settingsdialog.h"
#include "stickereditor.h"
#include "tour.h"
#include "uitheme.h"
#include "variantgallery.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QLabel>
#include <QLineEdit>
#include <QClipboard>
#include <QScrollArea>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

// ---------------------------------------------------------------------------------------------
// GIF89a writer: one global palette (the most frequent colours of all frames, nearest-colour
// mapping), each frame stores only the rectangle that changed, unchanged pixels inside it are
// transparent (better LZW runs), NETSCAPE2.0 loop.
class GifWriter {
  public:
    struct Frame {
        QImage image;
        int delayCs = 10; // hundredths of a second
    };

    static bool write(const QString &path, const QList<Frame> &frames) {
        if (frames.isEmpty()) return false;
        const QSize size = frames.front().image.size();
        QList<QImage> images;
        for (const Frame &frame : frames)
            images.append(frame.image.convertToFormat(QImage::Format_RGB32));
        const QList<QRgb> palette = choosePalette(images);
        QHash<QRgb, uchar> cache;
        const auto indexOf = [&](QRgb color) {
            color |= 0xff000000u;
            const auto found = cache.constFind(color);
            if (found != cache.constEnd()) return *found;
            int best = 0;
            int bestDistance = INT_MAX;
            for (int i = 0; i < palette.size(); ++i) {
                const int dr = qRed(color) - qRed(palette[i]);
                const int dg = qGreen(color) - qGreen(palette[i]);
                const int db = qBlue(color) - qBlue(palette[i]);
                const int distance = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = i;
                }
            }
            cache.insert(color, uchar(best));
            return uchar(best);
        };

        QByteArray out("GIF89a");
        const auto u16 = [&out](int value) {
            out.append(char(value & 0xff));
            out.append(char((value >> 8) & 0xff));
        };
        u16(size.width());
        u16(size.height());
        out.append(char(0xf7)); // global table, 8 bits per colour, 256 entries
        out.append(char(0));    // background index
        out.append(char(0));    // aspect
        for (int i = 0; i < 256; ++i) {
            const QRgb color = i < palette.size() ? palette[i] : qRgb(0, 0, 0);
            out.append(char(qRed(color)));
            out.append(char(qGreen(color)));
            out.append(char(qBlue(color)));
        }
        out.append("\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00", 19);

        QList<uchar> previous;
        for (int f = 0; f < images.size(); ++f) {
            const QImage &image = images[f];
            QList<uchar> indices(size.width() * size.height());
            for (int y = 0; y < size.height(); ++y) {
                const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
                for (int x = 0; x < size.width(); ++x)
                    indices[y * size.width() + x] = indexOf(line[x]);
            }
            QRect changed(QPoint(0, 0), size);
            if (!previous.isEmpty()) {
                int left = size.width(), right = -1, top = size.height(), bottom = -1;
                for (int y = 0; y < size.height(); ++y)
                    for (int x = 0; x < size.width(); ++x)
                        if (indices[y * size.width() + x] != previous[y * size.width() + x]) {
                            left = std::min(left, x);
                            right = std::max(right, x);
                            top = std::min(top, y);
                            bottom = std::max(bottom, y);
                        }
                changed = right < 0 ? QRect(0, 0, 1, 1) : QRect(QPoint(left, top), QPoint(right, bottom));
            }
            // Graphic control: do not dispose, transparent index kTransparent after the first frame.
            const bool transparent = !previous.isEmpty();
            out.append("\x21\xf9\x04", 3);
            out.append(char((1 << 2) | (transparent ? 1 : 0)));
            u16(frames[f].delayCs);
            out.append(char(kTransparent));
            out.append(char(0));
            // Image descriptor.
            out.append(char(0x2c));
            u16(changed.x());
            u16(changed.y());
            u16(changed.width());
            u16(changed.height());
            out.append(char(0));
            QByteArray pixels;
            pixels.reserve(changed.width() * changed.height());
            for (int y = changed.top(); y <= changed.bottom(); ++y)
                for (int x = changed.left(); x <= changed.right(); ++x) {
                    const int i = y * size.width() + x;
                    pixels.append(char(transparent && indices[i] == previous[i] ? kTransparent : indices[i]));
                }
            out.append(char(8)); // LZW minimum code size
            const QByteArray data = lzw(pixels);
            for (qsizetype at = 0; at < data.size(); at += 255) {
                const qsizetype length = std::min<qsizetype>(255, data.size() - at);
                out.append(char(length));
                out.append(data.mid(at, length));
            }
            out.append(char(0));
            previous = indices;
        }
        out.append(char(0x3b));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        return file.write(out) == out.size();
    }

  private:
    static constexpr int kTransparent = 255; // palette entries 0..254 are colours

    // The most frequent colours, skipping near duplicates so anti-aliased text keeps its ramps.
    static QList<QRgb> choosePalette(const QList<QImage> &images) {
        QHash<QRgb, qint64> counts;
        for (const QImage &image : images)
            for (int y = 0; y < image.height(); ++y) {
                const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
                for (int x = 0; x < image.width(); ++x)
                    ++counts[line[x] | 0xff000000u];
            }
        QList<std::pair<qint64, QRgb>> sorted;
        sorted.reserve(counts.size());
        for (auto it = counts.cbegin(); it != counts.cend(); ++it)
            sorted.append({it.value(), it.key()});
        std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        QList<QRgb> palette;
        for (const int minDistance : {48, 12, 0}) {
            for (const auto &[count, color] : std::as_const(sorted)) {
                if (palette.size() >= kTransparent) break;
                bool near = false;
                for (QRgb chosen : std::as_const(palette)) {
                    const int dr = qRed(color) - qRed(chosen), dg = qGreen(color) - qGreen(chosen),
                              db = qBlue(color) - qBlue(chosen);
                    if (dr * dr + dg * dg + db * db <= minDistance) {
                        near = true;
                        break;
                    }
                }
                if (!near) palette.append(color);
            }
        }
        return palette;
    }

    // Variable-length LZW as in the GIF89a specification (appendix F), codes packed LSB first.
    static QByteArray lzw(const QByteArray &pixels) {
        constexpr int kMinCode = 8, kClear = 1 << kMinCode, kEnd = kClear + 1;
        QByteArray out;
        quint32 buffer = 0;
        int bits = 0;
        int codeSize = kMinCode + 1;
        const auto putCode = [&](int code) {
            buffer |= quint32(code) << bits;
            bits += codeSize;
            while (bits >= 8) {
                out.append(char(buffer & 0xff));
                buffer >>= 8;
                bits -= 8;
            }
        };
        QHash<quint32, int> table; // (prefix code << 8 | byte) -> code
        int next = kEnd + 1;
        putCode(kClear);
        if (pixels.isEmpty()) {
            putCode(kEnd);
            if (bits > 0) out.append(char(buffer & 0xff));
            return out;
        }
        int prefix = uchar(pixels[0]);
        for (qsizetype i = 1; i < pixels.size(); ++i) {
            const uchar byte = uchar(pixels[i]);
            const quint32 key = (quint32(prefix) << 8) | byte;
            const auto found = table.constFind(key);
            if (found != table.constEnd()) {
                prefix = *found;
                continue;
            }
            putCode(prefix);
            if (next < 4096) {
                table.insert(key, next++);
                if (next > (1 << codeSize) && codeSize < 12) ++codeSize;
            } else {
                putCode(kClear);
                table.clear();
                next = kEnd + 1;
                codeSize = kMinCode + 1;
            }
            prefix = byte;
        }
        putCode(prefix);
        putCode(kEnd);
        if (bits > 0) out.append(char(buffer & 0xff));
        return out;
    }
};

// ---------------------------------------------------------------------------------------------
// Drives the window and the agents' tools.
class Media {
  public:
    // root: the repository; pictures go to its media/ and manual/images/ folders.
    explicit Media(QString root) : m_out(std::move(root)) {
        m_access.enabled = true;
        m_tools = McpMapTools::tools([this] { return m_access; });
    }

    void run(const QStringList &only);

  private:
    QJsonObject call(const QString &tool, QJsonObject args);
    QJsonObject readMap(const QString &path);
    QString id(const QString &title) const;
    QString file(const QString &name) const { return QDir(m_files.path()).filePath(name); }
    QAction *action(const QString &text) const;
    MindMapScene *scene() const { return m_window->findChild<MindMapScene *>(); }
    MindMapView *view() const { return m_window->findChild<MindMapView *>(); }
    MindMapModel *model() const { return m_window->findChild<MindMapModel *>(); }
    void startWindow(QSize size);
    void open(const QString &path);
    void wait(int ms = 150) { QTest::qWait(ms); }
    void fit(qreal margin = 40, qreal maxZoom = 1.0);
    void save(const QImage &image, const QString &name);
    void shot(const QString &name) { save(m_window->grab().toImage(), name); }
    // Single GIF frames as PNG for review, only when MENTIS_MEDIA_DEBUG names a folder.
    void debug(const QImage &image, const QString &name) const {
        const QString dir = qEnvironmentVariable("MENTIS_MEDIA_DEBUG");
        if (!dir.isEmpty()) image.save(QDir(dir).filePath(name), "PNG");
    }
    QImage frame() const;
    void writeGif(const QString &name, const QList<GifWriter::Frame> &frames) const {
        const QString path = QDir(m_out).filePath(name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        if (!GifWriter::write(path, frames)) {
            std::fprintf(stderr, "cannot write %s\n", qPrintable(path));
            std::exit(4);
        }
        std::printf("%s (%lld frames)\n", qPrintable(path), qlonglong(frames.size()));
    }

    void buildFiles();
    QString buildPhotosynthesis(const QString &name);
    QString buildStudyPlan(const QString &name);
    QString buildSimpleMap(const QString &name, const QString &title, const QString &markdown, const QString &palette,
                           int variant, const QString &structure = {});
    void renameFirstSheet(const QString &path, const QString &name);

    void mapShots(bool dark);
    void outlinerShot();
    void ganttShot();
    void pitchShot();
    void homeShot();
    void buildGif();
    void structuresGif();
    void themesGif();
    void windowShot();
    void updatesShot();
    void mapBasicsShots();
    void stylingShots();
    void contentsShots();
    void outlinerShots();
    void ganttShots();
    void pitchShots();
    void filesShots();
    void fasterShots();
    void settingsShots();
    void helpShots();
    void tourShots();
    // The side panel scrolled to the section with this title, cut out of the window.
    QImage panelSection(const QString &title);
    // The topic item with this title, summary topics included; exits if there is none.
    NodeItem *topic(const QString &title) const;
    // The window's picture cut to the scene rectangle (plus margin, in window pixels).
    QImage crop(const QRectF &sceneRect, int margin) const;
    QRectF around(const QStringList &titles) const;

    QString m_out;
    QTemporaryDir m_files;
    McpMapTools::Access m_access;
    QList<McpServer::Tool> m_tools;
    QJsonObject m_map; // the last read_map
    QString m_mapPath; // the main map
    QStringList m_homeMaps;
    QHash<QString, QString> m_sheetNames; // file -> name of its first sheet
    std::unique_ptr<MainWindow> m_window;
    QString m_nextOpen;
};

// GIF frames are this wide (the window is grabbed at 1280x800 and scaled by 3/4).
constexpr int kGifWidth = 960;
const QSize kShotSize(1440, 900);
const QSize kGifWindowSize(1280, 800);

QJsonObject Media::call(const QString &tool, QJsonObject args) {
    for (const McpServer::Tool &candidate : std::as_const(m_tools))
        if (candidate.name == tool) {
            const McpServer::ToolResult result = candidate.call(args);
            if (result.isError) {
                std::fprintf(stderr, "%s failed: %s\n", qPrintable(tool), qPrintable(result.text));
                std::exit(3);
            }
            return result.structured;
        }
    std::fprintf(stderr, "no tool %s\n", qPrintable(tool));
    std::exit(3);
}

QJsonObject Media::readMap(const QString &path) {
    m_map = call(QStringLiteral("read_map"), {{"path", path}});
    return m_map;
}

QString Media::id(const QString &title) const {
    QList<QJsonObject> pending{m_map.value("root").toObject()};
    while (!pending.isEmpty()) {
        const QJsonObject topic = pending.takeFirst();
        if (topic.value("title").toString() == title) return topic.value("id").toString();
        for (const QJsonValue &child : topic.value("children").toArray())
            pending.append(child.toObject());
    }
    std::fprintf(stderr, "no topic %s\n", qPrintable(title));
    std::exit(3);
}

QAction *Media::action(const QString &text) const {
    for (auto *candidate : m_window->findChildren<QAction *>())
        if (candidate->text() == text) return candidate;
    std::fprintf(stderr, "no action %s\n", qPrintable(text));
    std::exit(3);
}

void Media::startWindow(QSize size) {
    if (!m_window) {
        m_window = std::make_unique<MainWindow>();
        m_window->setOpenFilePicker([this] { return m_nextOpen; });
        m_window->show();
        if (!QTest::qWaitForWindowExposed(m_window.get())) std::exit(5);
    }
    if (m_window->size() != size) {
        m_window->resize(size);
        wait(300);
    }
}

void Media::open(const QString &path) {
    m_nextOpen = path;
    if (m_window->isWindowModified()) action(QStringLiteral("Save"))->trigger(); // else Open asks
    action(QStringLiteral("Open…"))->trigger();
    wait(400);
    auto *document = m_window->findChild<Document *>();
    if (m_sheetNames.contains(path) && document->renameSheet(document->currentSheet(), m_sheetNames.value(path))) {
        action(QStringLiteral("Save"))->trigger(); // saved at once: opening the next file asks nothing
        wait(100);
    }
    scene()->clearSelection();
}

// The whole map in the view, centred, zoomed in at most to maxZoom.
void Media::fit(qreal margin, qreal maxZoom) {
    const QRectF bounds = scene()->itemsBoundingRect().adjusted(-margin, -margin, margin, margin);
    view()->fitInView(bounds, Qt::KeepAspectRatio);
    if (view()->transform().m11() > maxZoom) view()->setTransform(QTransform::fromScale(maxZoom, maxZoom));
    view()->centerOn(bounds.center());
    wait(200);
}

void Media::save(const QImage &image, const QString &name) {
    const QString path = QDir(m_out).filePath(name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!image.save(path, "PNG")) {
        std::fprintf(stderr, "cannot write %s\n", qPrintable(path));
        std::exit(4);
    }
    std::printf("%s\n", qPrintable(path));
}

QImage Media::frame() const {
    return m_window->grab().toImage().scaledToWidth(kGifWidth, Qt::SmoothTransformation);
}

// ---------------------------------------------------------------------------------------------
// Content, written with the agents' tools before the window opens any file (an open map is locked).

QString Media::buildPhotosynthesis(const QString &name) {
    const QString path = file(name);
    const QString markdown = QStringLiteral("# Photosynthesis\n"
                                            "- Light reactions\n"
                                            "  - In the thylakoid membranes\n"
                                            "  - Water is split: O2 released\n"
                                            "  - ATP and NADPH are made\n"
                                            "- Calvin cycle\n"
                                            "  - In the stroma\n"
                                            "  - RuBisCO fixes CO2\n"
                                            "  - Builds glucose (G3P)\n"
                                            "- Inputs\n"
                                            "  - Sunlight\n"
                                            "  - Water (H2O)\n"
                                            "  - Carbon dioxide (CO2)\n"
                                            "- Outputs\n"
                                            "  - Glucose (C6H12O6)\n"
                                            "  - Oxygen (O2)\n"
                                            "- Limiting factors\n"
                                            "  - Light intensity\n"
                                            "  - CO2 concentration\n"
                                            "  - Temperature\n"
                                            "- Exam checklist\n"
                                            "  - Learn the equation\n"
                                            "  - Draw a chloroplast\n"
                                            "  - Past paper Q4\n");
    call(QStringLiteral("create_map"), {{"path", path}, {"markdown", markdown}});
    readMap(path);
    const auto sticker = [&](const char *title, const char *sticker, int width) {
        call(QStringLiteral("set_sticker"), {{"path", path},
                                             {"id", id(QString::fromLatin1(title))},
                                             {"sticker", QString::fromLatin1(sticker)},
                                             {"placement", "left"},
                                             {"width", width}});
    };
    sticker("Photosynthesis", ":/stickers/food/cactus.svg", 56);
    sticker("Light reactions", ":/stickers/objects/weather.svg", 40);
    sticker("Calvin cycle", ":/stickers/medical/test-tube.svg", 40);
    sticker("Exam checklist", ":/stickers/education/quiz.svg", 40);
    call(QStringLiteral("set_task"),
         {{"path", path}, {"ids", QJsonArray{id("Learn the equation"), id("Draw a chloroplast")}}, {"state", "done"}});
    call(QStringLiteral("set_task"), {{"path", path}, {"ids", QJsonArray{id("Past paper Q4")}}, {"state", "todo"}});
    const auto marker = [&](const char *title, const char *series, const char *marker) {
        call(QStringLiteral("set_marker"), {{"path", path},
                                            {"ids", QJsonArray{id(QString::fromLatin1(title))}},
                                            {"series", QString::fromLatin1(series)},
                                            {"marker", QString::fromLatin1(marker)}});
    };
    marker("RuBisCO fixes CO2", "symbol", "symbol:star");
    marker("Limiting factors", "priority", "priority:1");
    marker("Temperature", "symbol", "symbol:question");
    call(QStringLiteral("add_boundary"),
         {{"path", path},
          {"ids", QJsonArray{id("Sunlight"), id("Water (H2O)"), id("Carbon dioxide (CO2)")}},
          {"title", "Raw materials"}});
    call(QStringLiteral("add_summary"), {{"path", path},
                                         {"ids", QJsonArray{id("Glucose (C6H12O6)"), id("Oxygen (O2)")}},
                                         {"title", "Used in respiration"}});
    call(QStringLiteral("add_relationship"),
         {{"path", path}, {"from", id("ATP and NADPH are made")}, {"to", id("Calvin cycle")}, {"label", "powers"}});
    renameFirstSheet(path, QStringLiteral("Biology"));
    readMap(path);
    return path;
}

// rename_sheet on a file did not keep the name of a map's only sheet (seen 2026-11-02): the name is
// given in the window after opening instead.
void Media::renameFirstSheet(const QString &path, const QString &name) {
    m_sheetNames.insert(path, name);
}

QString Media::buildSimpleMap(const QString &name, const QString &title, const QString &markdown,
                              const QString &palette, int variant, const QString &structure) {
    const QString path = file(name);
    call(QStringLiteral("create_map"), {{"path", path}, {"markdown", "# " + title + "\n" + markdown}});
    QJsonObject theme{{"path", path}, {"variant", variant}};
    if (!palette.isEmpty()) theme.insert("palette", palette);
    call(QStringLiteral("set_theme"), theme);
    if (!structure.isEmpty()) {
        readMap(path);
        call(QStringLiteral("set_structure"), {{"path", path}, {"id", id(title)}, {"structure", structure}});
    }
    return path;
}

QString Media::buildStudyPlan(const QString &name) {
    const QString path = file(name);
    const QString markdown = QStringLiteral("# Thesis plan\n"
                                            "- Research\n"
                                            "  - Choose the topic\n"
                                            "  - Read the sources\n"
                                            "  - Interview two experts\n"
                                            "  - Outline approved\n"
                                            "- Writing\n"
                                            "  - Introduction\n"
                                            "  - Chapters 1-3\n"
                                            "  - Figures and tables\n"
                                            "  - Conclusion\n"
                                            "- Review\n"
                                            "  - Supervisor feedback\n"
                                            "  - Final edit\n"
                                            "  - Print and bind\n"
                                            "  - Submit thesis\n");
    call(QStringLiteral("create_map"), {{"path", path}, {"markdown", markdown}});
    renameFirstSheet(path, QStringLiteral("Thesis"));
    readMap(path);
    struct Task {
        const char *title;
        const char *start;
        int days;
        int progress;
    };
    const Task tasks[] = {
        {"Choose the topic", "2026-11-02", 2, 100},     {"Read the sources", "2026-11-04", 6, 80},
        {"Interview two experts", "2026-11-06", 4, 50}, {"Outline approved", "2026-11-10", 1, 0},
        {"Introduction", "2026-11-11", 3, 30},          {"Chapters 1-3", "2026-11-14", 8, 0},
        {"Figures and tables", "2026-11-17", 5, 0},     {"Conclusion", "2026-11-22", 2, 0},
        {"Supervisor feedback", "2026-11-24", 3, 0},    {"Final edit", "2026-11-27", 2, 0},
        {"Print and bind", "2026-11-29", 1, 0},         {"Submit thesis", "2026-11-30", 1, 0},
    };
    for (const Task &task : tasks)
        call(QStringLiteral("set_gantt_task"), {{"path", path},
                                                {"id", id(QString::fromLatin1(task.title))},
                                                {"start", QString::fromLatin1(task.start)},
                                                {"duration", task.days},
                                                {"progress", task.progress}});
    const std::pair<const char *, const char *> links[] = {
        {"Choose the topic", "Read the sources"},
        {"Read the sources", "Outline approved"},
        {"Interview two experts", "Outline approved"},
        {"Outline approved", "Introduction"},
        {"Introduction", "Chapters 1-3"},
        {"Chapters 1-3", "Conclusion"},
        {"Conclusion", "Supervisor feedback"},
        {"Supervisor feedback", "Final edit"},
        {"Final edit", "Print and bind"},
        {"Print and bind", "Submit thesis"},
    };
    for (const auto &[from, to] : links)
        call(QStringLiteral("add_dependency"),
             {{"path", path}, {"from", id(QString::fromLatin1(from))}, {"to", id(QString::fromLatin1(to))}});
    // A topic with a checkbox is a milestone (FEAT-048).
    call(QStringLiteral("set_task"),
         {{"path", path}, {"ids", QJsonArray{id("Outline approved"), id("Submit thesis")}}, {"state", "todo"}});
    return path;
}

void Media::buildFiles() {
    m_mapPath = buildPhotosynthesis(QStringLiteral("Photosynthesis.mentis"));
    call(QStringLiteral("set_pitch"), {{"path", m_mapPath}, {"theme", "playful"}, {"aspect", "16:9"}});
    readMap(m_mapPath);
    // The central topic's slide: its branches in a grid, one at a time without dimming the earlier ones
    // ("all" showed no items in the presentation window, seen 2026-11-02).
    call(QStringLiteral("set_pitch"),
         {{"path", m_mapPath}, {"id", id("Photosynthesis")}, {"layout", "grid"}, {"delivery", "one"}});
    call(QStringLiteral("set_pitch"), {{"path", m_mapPath}, {"id", id("Light reactions")}, {"layout", "numbered"}});
    call(QStringLiteral("set_pitch"), {{"path", m_mapPath}, {"id", id("Calvin cycle")}, {"layout", "circle"}});
    // The same map on a dark background: the Code palette's darkest colour (variant 5).
    const QString dark = file(QStringLiteral("Photosynthesis dark.mentis"));
    QFile::copy(m_mapPath, dark);
    renameFirstSheet(dark, QStringLiteral("Biology"));
    call(QStringLiteral("set_theme"), {{"path", dark}, {"palette", "codice"}, {"variant", 5}});
    buildStudyPlan(QStringLiteral("Thesis plan.mentis"));
    // The Home's cards: maps of other subjects, palettes and structures.
    struct Map {
        const char *file;
        const char *title;
        const char *markdown;
        const char *palette;
        int variant;
        const char *structure;
    };
    const Map maps[] = {
        {"World War I", "World War I",
         "- Causes\n  - Alliances\n  - Militarism\n  - Nationalism\n- Key battles\n  - Marne\n  - Verdun\n  - Somme\n"
         "- Home front\n  - Rationing\n  - Women at work\n- Peace\n  - Treaty of Versailles\n  - League of Nations\n",
         "", 1, ""},
        {"Spanish verbs", "Spanish verbs",
         "- Present\n  - hablo\n  - como\n  - vivo\n- Preterite\n  - hablé\n  - comí\n- Irregular\n  - ser\n  - ir\n"
         "  - tener\n",
         "vintage", 0, "logic"},
        {"The cell", "The cell",
         "- Nucleus\n  - DNA\n  - Nucleolus\n- Organelles\n  - Mitochondria\n  - Ribosomes\n  - Golgi body\n"
         "- Membrane\n  - Phospholipids\n  - Proteins\n",
         "oceano", 0, ""},
        {"Roman history", "Ancient Rome",
         "- Kingdom\n- Republic\n  - Punic wars\n  - Caesar\n- Empire\n  - Augustus\n  - Pax Romana\n- Fall\n  - 476 "
         "AD\n",
         "sofisticato", 1, "timeline"},
        {"Essay plan", "Essay: climate",
         "- Thesis\n- Arguments\n  - Data\n  - Examples\n  - Counterpoints\n- Conclusion\n", "menta", 0, "org"},
        {"Algebra", "Algebra",
         "- Equations\n  - Linear\n  - Quadratic\n- Functions\n  - Domain\n  - Graphs\n- Inequalities\n", "costanza", 0,
         ""},
        {"Why plants wilt", "Why plants wilt",
         "- Water\n  - Too little\n  - Too much\n- Light\n  - Not enough\n- Soil\n  - Poor drainage\n- Pests\n  - "
         "Aphids\n",
         "camino", 0, "fishbone"},
        {"Book report", "Romeo and Juliet",
         "- Characters\n  - Romeo\n  - Juliet\n  - Mercutio\n- Themes\n  - Love\n  - Fate\n- Acts\n  - I-II\n  - "
         "III-V\n",
         "rose", 0, ""},
        {"Chemistry", "Chemical bonds", "- Ionic\n  - NaCl\n- Covalent\n  - H2O\n  - CO2\n- Metallic\n  - Copper\n",
         "spazio", 0, "tree"},
        {"Trip to Lisbon", "Trip to Lisbon",
         "- Before\n  - Tickets\n  - Hostel\n- Sights\n  - Belém\n  - Alfama\n  - LX Factory\n- Food\n  - Pastéis\n",
         "caramella", 0, ""},
        {"French Revolution", "French Revolution",
         "- 1789\n  - Estates-General\n  - Bastille\n- Republic\n  - Terror\n  - Directory\n- Napoleon\n", "fiori", 0,
         "logic"},
        {"Probability", "Probability",
         "- Events\n  - Independent\n  - Dependent\n- Rules\n  - Addition\n  - Product\n- Distributions\n", "florido",
         0, ""},
    };
    m_homeMaps.clear();
    for (const Map &map : maps)
        m_homeMaps.append(buildSimpleMap(QString::fromUtf8(map.file) + QStringLiteral(".mentis"),
                                         QString::fromUtf8(map.title), QString::fromUtf8(map.markdown),
                                         QString::fromLatin1(map.palette), map.variant,
                                         QString::fromLatin1(map.structure)));
    buildSimpleMap(QStringLiteral("Structures.mentis"), QStringLiteral("World War I"),
                   QString::fromUtf8(maps[0].markdown), QString(), 0);
    renameFirstSheet(file(QStringLiteral("Structures.mentis")), QStringLiteral("History"));
    m_homeMaps.append(file(QStringLiteral("Thesis plan.mentis")));
    m_homeMaps.append(m_mapPath);
}

// ---------------------------------------------------------------------------------------------
// Screenshots.

void Media::mapShots(bool dark) {
    startWindow(kShotSize);
    open(dark ? file(QStringLiteral("Photosynthesis dark.mentis")) : m_mapPath);
    fit();
    shot(dark ? QStringLiteral("media/screenshot-map-dark.png") : QStringLiteral("media/screenshot-map.png"));
}

void Media::outlinerShot() {
    startWindow(kShotSize);
    open(m_mapPath);
    action(QStringLiteral("Switch between map and outliner"))->trigger();
    wait(300);
    // The central title's field takes its height from the laid-out text: one more resize settles it.
    m_window->resize(kShotSize + QSize(1, 0));
    wait(100);
    m_window->resize(kShotSize);
    wait(300);
    shot(QStringLiteral("media/screenshot-outliner.png"));
    action(QStringLiteral("Switch between map and outliner"))->trigger();
    wait(200);
}

void Media::ganttShot() {
    startWindow(kShotSize);
    open(file(QStringLiteral("Thesis plan.mentis")));
    action(QStringLiteral("Gantt chart"))->trigger();
    wait(500);
    shot(QStringLiteral("media/screenshot-gantt.png"));
    action(QStringLiteral("Switch between map and outliner"))->trigger();
    action(QStringLiteral("Switch between map and outliner"))->trigger();
    wait(200);
}

void Media::pitchShot() {
    startWindow(kShotSize);
    open(m_mapPath);
    PitchWindow window(Pitch::slides(*model()), Pitch::sheetLook(*model()), Pitch::Aspect::Wide, nullptr);
    window.resize(1440, 810);
    window.show();
    if (!QTest::qWaitForWindowExposed(&window)) std::exit(5);
    window.goTo(1, true); // the main branches in a grid
    wait(600);
    save(window.grab().toImage(), QStringLiteral("media/screenshot-pitch.png"));
    window.close();
}

void Media::homeShot() {
    startWindow(kShotSize);
    // create_map and the other tools store each file's Home preview: only the list of recent maps.
    QStringList recent;
    for (const QString &path : std::as_const(m_homeMaps))
        recent.prepend(path);
    QSettings().setValue(QStringLiteral("recentFiles"), recent);
    action(QStringLiteral("Library"))->trigger();
    wait(600);
    shot(QStringLiteral("media/screenshot-home.png"));
}

// ---------------------------------------------------------------------------------------------
// GIFs (window at 1280x800, frames scaled to 960 px, about 10 frames a second).

void Media::buildGif() {
    const QString path = file(QStringLiteral("Solar system.mentis"));
    call(QStringLiteral("create_map"), {{"path", path}, {"title", "Solar system"}});
    renameFirstSheet(path, QStringLiteral("Astronomy"));
    startWindow(kGifWindowSize);
    open(path);
    // 120%, with the zoom commands (the bottom bar shows it): the finished map about fills the frame.
    action(QStringLiteral("Actual size"))->trigger();
    action(QStringLiteral("Zoom in"))->trigger();
    action(QStringLiteral("Zoom in"))->trigger();
    view()->centerOn(scene()->itemsBoundingRect().center());
    m_window->activateWindow();
    if (!QTest::qWaitForWindowActive(m_window.get())) std::exit(5); // keys go to the active window
    view()->setFocus();
    QList<GifWriter::Frame> frames;
    const auto capture = [&](int delayCs) {
        wait(30);
        frames.append({frame(), delayCs});
    };
    const auto itemFor = [this](const MindNode *node) -> NodeItem * {
        for (QGraphicsItem *item : scene()->items())
            if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node() == node)
                return nodeItem;
        return nullptr;
    };
    // As Tab or Enter on the map (the commands of those keys), then the title typed two characters a
    // frame into the opened title; Enter ends the editing.
    const auto add = [&](const QString &command, const QString &text) {
        action(command)->trigger();
        capture(20);
        MindNode *node = scene()->selectedNode();
        itemFor(node)->beginEditing();
        QApplication::processEvents();
        for (int i = 0; i < text.size(); i += 2) {
            QTest::keyClicks(view()->viewport(), text.mid(i, 2));
            capture(10);
        }
        QTest::keyClick(view()->viewport(), Qt::Key_Return);
        capture(30);
        return node;
    };
    MindNode *root = model()->root();
    scene()->selectNode(root);
    capture(100);
    const QList<std::pair<QString, QStringList>> branches{
        {QStringLiteral("Inner planets"),
         {QStringLiteral("Mercury"), QStringLiteral("Venus"), QStringLiteral("Earth")}},
        {QStringLiteral("Outer planets"),
         {QStringLiteral("Jupiter"), QStringLiteral("Saturn"), QStringLiteral("Neptune")}},
        {QStringLiteral("Small bodies"), {QStringLiteral("Asteroids"), QStringLiteral("Comets")}},
        {QStringLiteral("The Sun"), {QStringLiteral("99.8% of the mass")}},
    };
    for (const auto &[branch, leaves] : branches) {
        scene()->selectNode(root);
        add(QStringLiteral("Add child"), branch);
        for (int i = 0; i < leaves.size(); ++i)
            add(i == 0 ? QStringLiteral("Add child") : QStringLiteral("Add sibling"), leaves[i]);
    }
    scene()->clearSelection();
    capture(300);
    writeGif(QStringLiteral("media/demo-build-map.gif"), frames);
    debug(frames[frames.size() / 2].image, QStringLiteral("build-middle.png"));
    debug(frames.last().image, QStringLiteral("build-last.png"));
}

void Media::structuresGif() {
    startWindow(kGifWindowSize);
    open(file(QStringLiteral("Structures.mentis")));
    action(QStringLiteral("Format panel: Map"))->trigger(); // the map type card shows the structure
    wait(300);
    auto *typeSelect = m_window->findChild<MapTypeSelect *>();
    QList<GifWriter::Frame> frames;
    // The first variant of each map type, chosen as from the card's gallery (one step each).
    using MapStructure::Type;
    for (const Type type : {Type::MindMap, Type::LogicChart, Type::OrgChart, Type::TreeChart, Type::Timeline,
                            Type::Fishbone, Type::Brace}) {
        const auto variants = MapVariants::all();
        const auto found =
            std::find_if(variants.begin(), variants.end(), [type](const auto &v) { return v.type == type; });
        if (found == variants.end()) continue;
        emit typeSelect->variantChosen(found->id);
        wait(150);
        fit(60, 1.3);
        frames.append({frame(), 140});
    }
    writeGif(QStringLiteral("media/demo-structures.gif"), frames);
    for (int i = 0; i < frames.size(); ++i)
        debug(frames[i].image, QStringLiteral("structure-%1.png").arg(i));
}

void Media::themesGif() {
    startWindow(kShotSize);
    open(m_mapPath);
    const QList<Pitch::Slide> slides = Pitch::slides(*model());
    QList<GifWriter::Frame> frames;
    const Pitch::Theme themes[] = {Pitch::Theme::Soft,     Pitch::Theme::Night,     Pitch::Theme::Paper,
                                   Pitch::Theme::Playful,  Pitch::Theme::Editorial, Pitch::Theme::Halftone,
                                   Pitch::Theme::Portfolio};
    const QSize size(kGifWidth, kGifWidth * 9 / 16);
    for (const Pitch::Theme theme : themes) {
        const Pitch::Look look = Pitch::look(*model(), theme);
        for (const int slide : {0, 1}) {
            QImage image(size, QImage::Format_RGB32);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::TextAntialiasing);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            Pitch::paint(&painter, QRectF(QPointF(0, 0), size), slides[slide], Pitch::lookOn(look, slides[slide]));
            painter.end();
            frames.append({image, slide == 0 ? 90 : 130});
        }
    }
    writeGif(QStringLiteral("media/demo-pitch-themes.gif"), frames);
}

// ---------------------------------------------------------------------------------------------
// User manual (manual/images/<group>/).

// The editor with numbered marks on its parts; manual/getting-started/window.md lists them in the
// same order. Each part is found by its tooltip (the save state by the start of it).
void Media::windowShot() {
    startWindow(kShotSize);
    open(m_mapPath);
    fit();
    const QStringList parts{QStringLiteral("Main menu"),
                            QStringLiteral("Open the library"),
                            QStringLiteral("Map saved"),
                            QStringLiteral("Add sibling"),
                            QStringLiteral("Create a relationship"),
                            QStringLiteral("Fit to window"),
                            QStringLiteral("Focus mode"),
                            QStringLiteral("User manual (F1)"),
                            QStringLiteral("Present (F5)"),
                            QStringLiteral("Markers and stickers"),
                            QStringLiteral("Show/hide side panel"),
                            QStringLiteral("Add sheet"),
                            QStringLiteral("Ctrl+wheel: zoom")};
    QImage image = m_window->grab().toImage();
    QPainter painter(&image);  // logical coordinates: the image keeps the window's pixel ratio
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font = QApplication::font();
    font.setPixelSize(13);
    font.setBold(true);
    painter.setFont(font);
    for (int i = 0; i < parts.size(); ++i) {
        QWidget *part = nullptr;
        for (QWidget *candidate : m_window->findChildren<QWidget *>())
            if (candidate->isVisible() && candidate->toolTip().startsWith(parts[i])) {
                part = candidate;
                break;
            }
        if (!part) {
            std::fprintf(stderr, "no part %s\n", qPrintable(parts[i]));
            std::exit(3);
        }
        // The sheets' mark on the sheet select, left of its "+" button.
        if (parts[i] == QLatin1String("Add sheet"))
            if (auto *select = m_window->findChild<QWidget *>(QStringLiteral("sheetSelect"))) part = select;
        const QRect box(part->mapTo(m_window.get(), QPoint(0, 0)), part->size());
        // Under the parts of the top bar, above those of the bottom bar.
        const bool top = box.center().y() < image.height() / image.devicePixelRatio() / 2;
        const QPointF centre(box.center().x(), top ? box.bottom() + 20 : box.top() - 20);
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(QColor(0x6d, 0x4a, 0xff));  // the accent of the New map button
        painter.drawEllipse(centre, 12, 12);
        painter.setPen(Qt::white);
        painter.drawText(QRectF(centre.x() - 12, centre.y() - 12, 24, 24), Qt::AlignCenter, QString::number(i + 1));
    }
    painter.end();
    save(image, QStringLiteral("manual/images/getting-started/window.png"));
}

// Settings > General, scrolled to the Updates card.
void Media::updatesShot() {
    startWindow(kShotSize);
    SettingsDialog dialog(m_window.get());
    dialog.show();
    if (!QTest::qWaitForWindowExposed(&dialog)) std::exit(5);
    wait(300);
    auto *updates = dialog.findChild<QWidget *>(QStringLiteral("UpdatesSwitch"));
    if (!updates) std::exit(3);
    for (QWidget *parent = updates->parentWidget(); parent; parent = parent->parentWidget())
        if (auto *scroll = qobject_cast<QScrollArea *>(parent)) {
            scroll->ensureWidgetVisible(updates, 0, 120);
            break;
        }
    wait(200);
    save(dialog.grab().toImage(), QStringLiteral("manual/images/getting-started/install-updates.png"));
    dialog.close();
}

NodeItem *Media::topic(const QString &title) const {
    for (QGraphicsItem *item : scene()->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node()->title == title)
            return nodeItem;
    std::fprintf(stderr, "no topic item %s\n", qPrintable(title));
    std::exit(3);
}

QRectF Media::around(const QStringList &titles) const {
    QRectF box;
    for (const QString &title : titles) box |= topic(title)->sceneBoundingRect();
    return box;
}

QImage Media::crop(const QRectF &sceneRect, int margin) const {
    const QImage image = m_window->grab().toImage();
    const QPoint origin = view()->viewport()->mapTo(m_window.get(), QPoint(0, 0));
    const QRect box =
        view()->mapFromScene(sceneRect).boundingRect().translated(origin).adjusted(-margin, -margin, margin, margin) &
        m_window->rect();
    const qreal ratio = image.devicePixelRatio();
    return image.copy(QRect((QPointF(box.topLeft()) * ratio).toPoint(), (QSizeF(box.size()) * ratio).toSize()));
}

// manual/map: crops of the Photosynthesis map around what each page explains, and the
// Map page of the Format panel. Every change made here is undone before the next picture.
void Media::mapBasicsShots() {
    startWindow(kShotSize);
    open(m_mapPath);
    fit();
    const QString dir = QStringLiteral("manual/images/map/");
    // A selected topic: its round "+" knobs for a subtopic and a topic after.
    scene()->selectNode(topic(QStringLiteral("Calvin cycle"))->node());
    wait(200);
    save(crop(around({QStringLiteral("Calvin cycle"), QStringLiteral("Builds glucose (G3P)")}), 40),
         dir + QStringLiteral("topics.png"));
    // A collapsed branch shows how many topics it hides.
    scene()->selectNode(topic(QStringLiteral("Light reactions"))->node());
    action(QStringLiteral("Collapse or expand the branch"))->trigger();
    wait(300);
    save(crop(around({QStringLiteral("Light reactions")}), 60), dir + QStringLiteral("collapse.png"));
    action(QStringLiteral("Collapse or expand the branch"))->trigger();
    scene()->clearSelection();
    wait(300);
    save(crop(around({QStringLiteral("ATP and NADPH are made"), QStringLiteral("Calvin cycle")}), 30),
         dir + QStringLiteral("relationships.png"));
    save(crop(around({QStringLiteral("Sunlight"), QStringLiteral("Carbon dioxide (CO2)")}), 40),
         dir + QStringLiteral("boundaries.png"));
    save(crop(around({QStringLiteral("Used in respiration"), QStringLiteral("Outputs")}), 30),
         dir + QStringLiteral("summaries.png"));
    // The sheets at the bottom left.
    if (auto *sheets = m_window->findChild<QWidget *>(QStringLiteral("sheetSelect"))) {
        QWidget *bar = sheets->parentWidget();
        const QRect box = QRect(bar->mapTo(m_window.get(), QPoint(0, 0)), bar->size()).adjusted(-16, -16, 16, 16);
        const QImage image = m_window->grab().toImage();
        const qreal ratio = image.devicePixelRatio();
        save(image.copy(QRect((QPointF(box.topLeft()) * ratio).toPoint(), (QSizeF(box.size()) * ratio).toSize())),
             dir + QStringLiteral("sheets.png"));
    }
    // The Map page: map type, theme, balance; nothing selected.
    action(QStringLiteral("Format panel: Map"))->trigger();
    wait(500);
    fit();
    shot(dir + QStringLiteral("map-page.png"));
    action(QStringLiteral("Format panel: Map"))->trigger();
    wait(300);
    // Numbered branches, then undone.
    scene()->selectNode(model()->root());
    action(QStringLiteral("Numbering: 1.2.3"))->trigger();
    scene()->clearSelection();
    wait(300);
    fit();
    save(crop(scene()->itemsBoundingRect(), 0), dir + QStringLiteral("numbering.png"));
    action(QStringLiteral("Undo"))->trigger();
    wait(300);
}

QImage Media::panelSection(const QString &title) {
    // The heading: a label, or the button that folds the section.
    QWidget *heading = nullptr;
    for (QWidget *widget : m_window->findChildren<QWidget *>()) {
        if (!widget->isVisible()) continue;
        const auto *label = qobject_cast<QLabel *>(widget);
        const auto *button = qobject_cast<QAbstractButton *>(widget);
        // A section's button reads "▾ Title".
        if ((label && label->text() == title) ||
            (button && (button->text() == title || button->text().endsWith(QLatin1Char(' ') + title)))) {
            heading = widget;
            break;
        }
    }
    if (!heading) {
        std::fprintf(stderr, "no panel section %s\n", qPrintable(title));
        std::exit(3);
    }
    QScrollArea *scroll = nullptr;
    for (QWidget *parent = heading->parentWidget(); parent && !scroll; parent = parent->parentWidget())
        scroll = qobject_cast<QScrollArea *>(parent);
    if (!scroll) std::exit(3);
    // The heading at the top of the visible part.
    scroll->verticalScrollBar()->setValue(heading->mapTo(scroll->widget(), QPoint(0, 0)).y() - 8);
    wait(200);
    const QImage image = m_window->grab().toImage();
    const QRect box(scroll->mapTo(m_window.get(), QPoint(0, 0)), scroll->size());
    const qreal ratio = image.devicePixelRatio();
    return image.copy(QRect((QPointF(box.topLeft()) * ratio).toPoint(), (QSizeF(box.size()) * ratio).toSize()));
}

// manual/styling: the shapes, the Style page, its Text and Branch sections, the map in seven themes.
void Media::stylingShots() {
    const QString dir = QStringLiteral("manual/images/style/");
    // Twelve topics, each in another shape (written before the window opens the file).
    const QString shapes = file(QStringLiteral("Shapes.mentis"));
    const QList<std::pair<QString, QString>> kinds{
        {QStringLiteral("Rounded rectangle"), QStringLiteral("rettangoloArrotondato")},
        {QStringLiteral("Rectangle"), QStringLiteral("rettangolo")},
        {QStringLiteral("Pill"), QStringLiteral("pillola")},
        {QStringLiteral("Ellipse"), QStringLiteral("ellisse")},
        {QStringLiteral("Hexagon"), QStringLiteral("esagono")},
        {QStringLiteral("Diamond"), QStringLiteral("rombo")},
        {QStringLiteral("Cloud"), QStringLiteral("nuvola")},
        {QStringLiteral("Star"), QStringLiteral("stella")},
        {QStringLiteral("Heart"), QStringLiteral("cuore")},
        {QStringLiteral("Flag"), QStringLiteral("bandiera")},
        {QStringLiteral("Database"), QStringLiteral("database")},
        {QStringLiteral("Line below"), QStringLiteral("lineaSotto")},
    };
    QString markdown = QStringLiteral("# Shapes\n");
    for (const auto &[name, key] : kinds) markdown += QStringLiteral("- %1\n").arg(name);
    call(QStringLiteral("create_map"), {{"path", shapes}, {"markdown", markdown}});
    readMap(shapes);
    for (const auto &[name, key] : kinds)
        call(QStringLiteral("set_style"), {{"path", shapes}, {"ids", QJsonArray{id(name)}}, {"shape", key}});
    renameFirstSheet(shapes, QStringLiteral("Shapes"));
    // The same map in other colour themes, as separate files.
    const QStringList palettes{QString(), QStringLiteral("oceano"), QStringLiteral("menta"),
                               QStringLiteral("sofisticato"), QStringLiteral("caramella"), QStringLiteral("vintage"),
                               QStringLiteral("codice")};
    QStringList themed;
    for (int i = 0; i < palettes.size(); ++i) {
        const QString copy = file(QStringLiteral("Photosynthesis theme %1.mentis").arg(i));
        QFile::remove(copy);
        QFile::copy(m_mapPath, copy);
        if (!palettes[i].isEmpty()) call(QStringLiteral("set_theme"), {{"path", copy}, {"palette", palettes[i]}, {"variant", 0}});
        renameFirstSheet(copy, QStringLiteral("Biology"));
        themed.append(copy);
    }

    startWindow(kShotSize);
    open(shapes);
    fit(40, 1.2);
    save(crop(scene()->itemsBoundingRect(), 0), dir + QStringLiteral("shapes.png"));

    open(m_mapPath);
    fit();
    scene()->selectNode(topic(QStringLiteral("Calvin cycle"))->node());
    action(QStringLiteral("Format panel: Style"))->trigger();
    wait(500);
    shot(dir + QStringLiteral("style-page.png"));
    // From Text down: Text, Structure, Branch, Numbering and the buttons (the panel ends there).
    save(panelSection(QStringLiteral("Text")), dir + QStringLiteral("style-page-lower.png"));
    action(QStringLiteral("Format panel: Style"))->trigger();
    scene()->clearSelection();
    wait(300);

    startWindow(kGifWindowSize);
    QList<GifWriter::Frame> frames;
    for (const QString &path : std::as_const(themed)) {
        open(path);
        fit();
        frames.append({frame(), 120});
    }
    writeGif(dir + QStringLiteral("themes.gif"), frames);
    for (int i = 0; i < frames.size(); ++i) debug(frames[i].image, QStringLiteral("theme-%1.png").arg(i));
}

// manual/topic-contents: a chemistry map with a note, labels, checkboxes, a picture, a formula, a
// web link, an attached file, markers and a sticker; crops of each, the Markers and Sticker pages
// and the sticker editor.
void Media::contentsShots() {
    const QString dir = QStringLiteral("manual/images/topic-contents/");
    const QString path = file(QStringLiteral("Chemistry exam.mentis"));
    call(QStringLiteral("create_map"), {{"path", path},
                                        {"markdown", QStringLiteral("# Chemistry exam\n"
                                                                    "- Reaction rates\n"
                                                                    "- Ideal gas law\n"
                                                                    "- Periodic table\n"
                                                                    "- Revision\n"
                                                                    "  - Flashcards\n"
                                                                    "  - Past papers\n"
                                                                    "  - Mock exam\n"
                                                                    "- Lab report\n"
                                                                    "- Reading\n")}});
    readMap(path);
    call(QStringLiteral("update_topic"),
         {{"path", path},
          {"id", id(QStringLiteral("Reaction rates"))},
          {"notes", QStringLiteral("Rate = change in concentration / time. Faster with heat, a catalyst, more "
                                   "surface or a higher concentration.")},
          {"labels", QJsonArray{QStringLiteral("exam"), QStringLiteral("week 3")}}});
    call(QStringLiteral("set_formula"), {{"path", path}, {"id", id(QStringLiteral("Ideal gas law"))}, {"latex", "PV = nRT"}});
    // A small periodic-table picture, drawn here.
    const QString picture = file(QStringLiteral("periodic-table.png"));
    {
        QImage image(360, 200, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        const QStringList symbols{"H", "He", "Li", "Be", "B", "C", "N", "O", "F", "Ne", "Na", "Mg", "Al", "Si", "P",
                                  "S", "Cl", "Ar"};
        const QColor colours[] = {QColor(0xff, 0xb3, 0xa7), QColor(0xa7, 0xd8, 0xff), QColor(0xc8, 0xf0, 0xb4),
                                  QColor(0xff, 0xe0, 0x8a)};
        QFont font = QApplication::font();
        font.setPixelSize(18);
        font.setBold(true);
        painter.setFont(font);
        for (int i = 0; i < symbols.size(); ++i) {
            const QRectF cell(12 + (i % 6) * 56, 12 + (i / 6) * 60, 50, 54);
            painter.setPen(Qt::NoPen);
            painter.setBrush(colours[i % 4]);
            painter.drawRoundedRect(cell, 6, 6);
            painter.setPen(QColor(0x20, 0x26, 0x30));
            painter.drawText(cell, Qt::AlignCenter, symbols[i]);
        }
        painter.end();
        image.save(picture, "PNG");
    }
    call(QStringLiteral("set_image"), {{"path", path}, {"id", id(QStringLiteral("Periodic table"))}, {"file", picture}});
    call(QStringLiteral("set_task"),
         {{"path", path}, {"ids", QJsonArray{id(QStringLiteral("Flashcards")), id(QStringLiteral("Past papers"))}}, {"state", "done"}});
    call(QStringLiteral("set_task"), {{"path", path}, {"ids", QJsonArray{id(QStringLiteral("Mock exam"))}}, {"state", "todo"}});
    const QString report = file(QStringLiteral("Lab report.txt"));
    {
        QFile out(report);
        if (out.open(QIODevice::WriteOnly)) out.write("Titration of HCl with NaOH\n");
    }
    call(QStringLiteral("attach_files"), {{"path", path}, {"id", id(QStringLiteral("Lab report"))}, {"files", QJsonArray{report}}});
    call(QStringLiteral("set_links"),
         {{"path", path}, {"id", id(QStringLiteral("Reading"))}, {"web", QStringLiteral("https://en.wikipedia.org/wiki/Chemistry")}});
    const auto marker = [&](const char *title, const char *series, const char *value) {
        call(QStringLiteral("set_marker"), {{"path", path},
                                            {"ids", QJsonArray{id(QString::fromLatin1(title))}},
                                            {"series", QString::fromLatin1(series)},
                                            {"marker", QString::fromLatin1(value)}});
    };
    marker("Reaction rates", "priority", "priority:1");
    marker("Revision", "progress", "progress:50");
    marker("Lab report", "flag", "flag:red");
    call(QStringLiteral("set_sticker"), {{"path", path},
                                         {"id", id(QStringLiteral("Chemistry exam"))},
                                         {"sticker", QStringLiteral(":/stickers/medical/test-tube.svg")},
                                         {"placement", "left"},
                                         {"width", 56}});
    renameFirstSheet(path, QStringLiteral("Chemistry"));

    startWindow(kShotSize);
    open(path);
    fit();
    shot(dir + QStringLiteral("contents.png"));
    save(crop(around({QStringLiteral("Reaction rates")}), 40), dir + QStringLiteral("labels.png"));
    save(crop(around({QStringLiteral("Ideal gas law")}), 30), dir + QStringLiteral("formulas.png"));
    save(crop(around({QStringLiteral("Revision"), QStringLiteral("Mock exam")}), 30), dir + QStringLiteral("checkboxes.png"));
    save(crop(around({QStringLiteral("Lab report"), QStringLiteral("Reading")}), 30), dir + QStringLiteral("links-and-files.png"));
    // The picture selected: its corner handles.
    scene()->selectNode(topic(QStringLiteral("Periodic table"))->node());
    wait(200);
    save(crop(around({QStringLiteral("Periodic table")}), 40), dir + QStringLiteral("images.png"));
    // The note editor beside its topic.
    scene()->selectNode(topic(QStringLiteral("Reaction rates"))->node());
    // The command (Ctrl+Shift+N), not the outliner's menu item of the same name.
    for (QAction *candidate : m_window->findChildren<QAction *>())
        if (candidate->text() == QLatin1String("Note") && !candidate->shortcut().isEmpty()) {
            candidate->trigger();
            break;
        }
    wait(400);
    shot(dir + QStringLiteral("notes.png"));
    QTest::keyClick(QApplication::focusWidget() ? QApplication::focusWidget() : m_window.get(), Qt::Key_Escape);
    wait(200);
    scene()->clearSelection();
    // The Markers and Sticker pages of the side panel.
    scene()->selectNode(topic(QStringLiteral("Lab report"))->node());
    action(QStringLiteral("Marker"))->trigger();
    wait(500);
    shot(dir + QStringLiteral("markers.png"));
    action(QStringLiteral("Sticker…"))->trigger();
    wait(500);
    shot(dir + QStringLiteral("stickers.png"));
    scene()->clearSelection();
    // The sticker editor on a built-in sticker.
    const QList<IconLibrary::Collection> collections = IconLibrary::collections(IconLibrary::Kind::Sticker);
    StickerEditorDialog editor(m_window.get(), QStringLiteral(":/stickers/education/quiz.svg"), collections,
                               collections.isEmpty() ? QString() : collections.first().id);
    editor.show();
    if (!QTest::qWaitForWindowExposed(&editor)) std::exit(5);
    wait(500);
    save(editor.grab().toImage(), dir + QStringLiteral("sticker-editor.png"));
    editor.reject();
}

// manual/outliner: Markdown shortcuts typed in a note (GIF), and a Markdown list pasted into the
// outliner (before and after, GIF). Both changes are undone.
void Media::outlinerShots() {
    const QString dir = QStringLiteral("manual/images/outliner/");
    startWindow(kShotSize);
    open(m_mapPath);
    fit();
    scene()->selectNode(topic(QStringLiteral("Calvin cycle"))->node());
    for (QAction *candidate : m_window->findChildren<QAction *>())
        if (candidate->text() == QLatin1String("Note") && !candidate->shortcut().isEmpty()) {
            candidate->trigger();
            break;
        }
    wait(300);
    NoteEditor *note = m_window->noteEditor();
    if (!note || !note->isVisible()) {
        std::fprintf(stderr, "no note editor\n");
        std::exit(3);
    }
    // One rectangle for every frame (a GIF has one size): the editor grows as lines are added.
    const QRect box = (QRect(note->mapTo(m_window.get(), QPoint(0, 0)), note->size()).adjusted(-24, -24, 24, 120)) &
                      m_window->rect();
    const auto noteFrame = [&] {
        const QImage image = m_window->grab().toImage();
        const qreal ratio = image.devicePixelRatio();
        return image.copy(QRect((QPointF(box.topLeft()) * ratio).toPoint(), (QSizeF(box.size()) * ratio).toSize()));
    };
    QList<GifWriter::Frame> frames;
    frames.append({noteFrame(), 80});
    const auto type = [&](const QString &text) {
        for (int i = 0; i < text.size(); i += 2) {
            QTest::keyClicks(note->textEdit(), text.mid(i, 2));
            wait(20);
            frames.append({noteFrame(), 8});
        }
    };
    type(QStringLiteral("Fixes **carbon dioxide** into sugar."));
    frames.last().delayCs = 80;
    QTest::keyClick(note->textEdit(), Qt::Key_Return);
    type(QStringLiteral("- Needs ATP"));
    QTest::keyClick(note->textEdit(), Qt::Key_Return);
    type(QStringLiteral("Needs ==NADPH=="));
    frames.last().delayCs = 250;
    writeGif(dir + QStringLiteral("markdown-shortcuts.gif"), frames);
    debug(frames.last().image, QStringLiteral("markdown-last.png"));
    QTest::keyClick(note->textEdit(), Qt::Key_Escape);
    wait(200);
    action(QStringLiteral("Undo"))->trigger();
    scene()->clearSelection();
    wait(200);

    // A Markdown list pasted with nothing selected goes to the end of the first level.
    startWindow(kGifWindowSize);
    renameFirstSheet(file(QStringLiteral("Essay plan.mentis")), QStringLiteral("English"));
    open(file(QStringLiteral("Essay plan.mentis")));
    action(QStringLiteral("Switch between map and outliner"))->trigger();
    wait(300);
    m_window->resize(kGifWindowSize + QSize(1, 0));  // the top title's field settles (see outlinerShot)
    wait(100);
    m_window->resize(kGifWindowSize);
    wait(300);
    QList<GifWriter::Frame> paste{{frame(), 150}};
    QApplication::clipboard()->setText(
        QStringLiteral("- Sources\n  - IPCC report\n  - NASA climate data\n  - Interview notes\n"));
    auto *tree = m_window->findChild<QTreeView *>(QStringLiteral("OutlineTree"));
    if (!tree) std::exit(3);
    tree->clearSelection();
    tree->setCurrentIndex(QModelIndex());
    tree->setFocus();
    QTest::keyClick(tree, Qt::Key_V, Qt::ControlModifier);
    wait(400);
    paste.append({frame(), 250});
    writeGif(dir + QStringLiteral("paste-markdown.gif"), paste);
    debug(paste.last().image, QStringLiteral("paste-after.png"));
    action(QStringLiteral("Undo"))->trigger();
    action(QStringLiteral("Switch between map and outliner"))->trigger();
    wait(200);
}

// manual/gantt: the thesis plan with two people, a snapshot and a task that slipped; the task panel,
// the Gantt style page, the critical path, the baseline compared and the Resources dialog.
void Media::ganttShots() {
    const QString dir = QStringLiteral("manual/images/gantt/");
    const QString path = file(QStringLiteral("Thesis gantt.mentis"));
    QFile::remove(path);
    QFile::copy(file(QStringLiteral("Thesis plan.mentis")), path);
    renameFirstSheet(path, QStringLiteral("Thesis"));
    readMap(path);
    const QJsonObject student = call(QStringLiteral("add_person"), {{"path", path},
                                                                     {"first_name", "Ana"},
                                                                     {"last_name", "Silva"},
                                                                     {"role", "Student"},
                                                                     {"hourly_cost", 15}});
    const QJsonObject supervisor = call(QStringLiteral("add_person"), {{"path", path},
                                                                        {"first_name", "Marco"},
                                                                        {"last_name", "Rossi"},
                                                                        {"role", "Supervisor"},
                                                                        {"hourly_cost", 40}});
    QJsonArray studentTasks;
    for (const char *title : {"Choose the topic", "Read the sources", "Interview two experts", "Introduction",
                              "Chapters 1-3", "Figures and tables", "Conclusion", "Final edit", "Print and bind"})
        studentTasks.append(id(QString::fromLatin1(title)));
    call(QStringLiteral("assign_people"),
         {{"path", path}, {"ids", studentTasks}, {"assign", QJsonArray{student.value("id").toString()}}});
    call(QStringLiteral("assign_people"),
         {{"path", path},
          {"ids", QJsonArray{id("Supervisor feedback"), id("Outline approved")}},
          {"assign", QJsonArray{supervisor.value("id").toString()}},
          {"percent", 50}});
    call(QStringLiteral("add_baseline"), {{"path", path}, {"name", "First plan"}});
    call(QStringLiteral("set_gantt_task"), {{"path", path}, {"id", id("Chapters 1-3")}, {"duration", 11}});

    startWindow(kShotSize);
    open(path);
    action(QStringLiteral("Gantt chart"))->trigger();
    wait(500);
    auto *gantt = m_window->findChild<GanttView *>();
    auto *document = m_window->findChild<Document *>();
    if (!gantt || !document) std::exit(3);
    // The task panel for the task that slipped, then the sheet's Gantt style.
    gantt->selectTask(id("Chapters 1-3"));
    gantt->togglePanel();
    wait(400);
    gantt->panel()->showPage(GanttTaskPanel::Page::Task);
    wait(300);
    shot(dir + QStringLiteral("task-panel.png"));
    gantt->panel()->showPage(GanttTaskPanel::Page::Gantt);
    wait(300);
    shot(dir + QStringLiteral("gantt-style.png"));
    gantt->togglePanel();
    wait(300);
    // The critical path (off in this tool's settings), then the snapshot compared.
    if (auto *critical = m_window->findChild<QToolButton *>(QStringLiteral("ganttCriticalButton"))) {
        critical->click();
        wait(300);
        shot(dir + QStringLiteral("critical-path.png"));
        critical->click();
        wait(200);
    }
    const GanttPlan plan = document->model()->gantt();  // by value: keep it while its baselines are read
    const QVector<GanttBaseline> &baselines = plan.baselines();
    if (!baselines.isEmpty()) {
        gantt->compareBaseline(baselines.first().id);
        wait(400);
        shot(dir + QStringLiteral("baseline.png"));
        gantt->compareBaseline(QString());
        wait(200);
    }
    // The Resources dialog: people, currency, working week.
    ResourcesDialog resources(m_window.get(), document->resources(), document->model()->gantt().calendar());
    resources.show();
    if (!QTest::qWaitForWindowExposed(&resources)) std::exit(5);
    wait(300);
    save(resources.grab().toImage(), dir + QStringLiteral("resources.png"));
    resources.reject();
    action(QStringLiteral("Switch between map and outliner"))->trigger();
    action(QStringLiteral("Switch between map and outliner"))->trigger();
    wait(200);
}

// manual/pitch: the Pitch tab for a branch, the template editor, and the central topic's list
// slide in the ten layouts (GIF).
void Media::pitchShots() {
    const QString dir = QStringLiteral("manual/images/pitch/");
    startWindow(kShotSize);
    open(m_mapPath);
    fit();
    scene()->selectNode(topic(QStringLiteral("Light reactions"))->node());
    action(QStringLiteral("Format panel: Pitch"))->trigger();
    wait(500);
    shot(dir + QStringLiteral("pitch-tab.png"));
    if (auto *page = m_window->findChild<PitchPage *>()) {
        page->newTemplate();
        wait(400);
        shot(dir + QStringLiteral("template-editor.png"));
        page->cancelEditing();
        wait(200);
    }
    action(QStringLiteral("Format panel: Pitch"))->trigger();
    scene()->clearSelection();
    wait(300);

    const QList<Pitch::Slide> slides = Pitch::slides(*model());
    const auto list = std::find_if(slides.cbegin(), slides.cend(),
                                   [](const Pitch::Slide &slide) { return slide.kind == Pitch::Slide::Kind::List; });
    if (list == slides.cend()) std::exit(3);
    const Pitch::Look look = Pitch::sheetLook(*model());
    const QSize size(kGifWidth, kGifWidth * 9 / 16);
    QList<GifWriter::Frame> frames;
    for (int layout = 0; layout < Pitch::kLayoutCount; ++layout) {
        Pitch::Slide slide = *list;
        slide.layout = static_cast<Pitch::Layout>(layout);
        QImage image(size, QImage::Format_RGB32);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        Pitch::paint(&painter, QRectF(QPointF(0, 0), size), slide, Pitch::lookOn(look, slide));
        painter.end();
        frames.append({image, 120});
    }
    writeGif(dir + QStringLiteral("layouts.gif"), frames);
    for (int i = 0; i < frames.size(); ++i) debug(frames[i].image, QStringLiteral("layout-%1.png").arg(i));
}

// manual/files: the Export image and Export PDF windows (the PDF one with a frame), grabbed while
// open and then cancelled.
void Media::filesShots() {
    const QString dir = QStringLiteral("manual/images/files/");
    startWindow(kShotSize);
    open(m_mapPath);
    fit();
    // The window opens modal: a timer grabs it inside its own event loop, then cancels it.
    const auto grabDialog = [this](const QString &command, const QString &name, const std::function<void(QWidget *)> &prepare) {
        QTimer::singleShot(800, [this, name, prepare] {
            QWidget *dialog = QApplication::activeModalWidget();
            if (!dialog) return;
            if (prepare) prepare(dialog);
            QTest::qWait(500);
            save(dialog->grab().toImage(), name);
            if (auto *modal = qobject_cast<QDialog *>(dialog)) modal->reject();
        });
        action(command)->trigger();
        wait(200);
    };
    grabDialog(QStringLiteral("Export image…"), dir + QStringLiteral("export-image.png"), {});
    grabDialog(QStringLiteral("Export PDF…"), dir + QStringLiteral("export-pdf.png"), [](QWidget *dialog) {
        if (auto *frame = dialog->findChild<QAbstractButton *>(QStringLiteral("ExportFrame"))) frame->setChecked(true);
    });
}

// manual/map and manual/settings: the search panel with results, and Settings > Shortcuts (grabbed while open).
void Media::fasterShots() {
    const QString dir = QStringLiteral("manual/images/settings/");
    startWindow(kShotSize);
    open(m_mapPath);
    fit();
    action(QStringLiteral("Find"))->trigger();
    wait(400);
    QLineEdit *field = nullptr;
    for (QLineEdit *candidate : m_window->findChildren<QLineEdit *>())
        if (candidate->isVisible() && candidate->placeholderText() == QLatin1String("Find")) field = candidate;
    if (!field) {
        std::fprintf(stderr, "no search field\n");
        std::exit(3);
    }
    field->setFocus();
    QTest::keyClicks(field, QStringLiteral("CO2"));
    wait(600);
    shot(QStringLiteral("manual/images/map/search.png"));
    action(QStringLiteral("Find"))->trigger();  // closes the panel
    wait(300);

    QTimer::singleShot(800, [this, dir] {
        QWidget *dialog = QApplication::activeModalWidget();
        if (!dialog) return;
        save(dialog->grab().toImage(), dir + QStringLiteral("shortcuts.png"));
        if (auto *modal = qobject_cast<QDialog *>(dialog)) modal->reject();
    });
    action(QStringLiteral("Shortcuts…"))->trigger();
    wait(200);

    // Settings > Editor: Markdown shortcuts and, on Windows, Check spelling.
    SettingsDialog editor(m_window.get());
    editor.showPage(2);
    editor.show();
    if (!QTest::qWaitForWindowExposed(&editor)) std::exit(5);
    wait(300);
    for (QLabel *label : editor.findChildren<QLabel *>())
        if (label->isVisible() && label->text() == QLatin1String("Writing")) {
            for (QWidget *parent = label->parentWidget(); parent; parent = parent->parentWidget())
                if (auto *scroll = qobject_cast<QScrollArea *>(parent)) {
                    scroll->verticalScrollBar()->setValue(label->mapTo(scroll->widget(), QPoint(0, 0)).y() - 12);
                    break;
                }
            break;
        }
    wait(200);
    save(editor.grab().toImage(), dir + QStringLiteral("settings-editor.png"));
    editor.close();
}

// manual/settings: the General, Appearance and Editor tabs, each from the top.
void Media::settingsShots() {
    const QString dir = QStringLiteral("manual/images/settings/");
    startWindow(kShotSize);
    const QStringList names{QStringLiteral("general.png"), QStringLiteral("appearance.png"), QStringLiteral("editor.png")};
    for (int page = 0; page < names.size(); ++page) {
        SettingsDialog dialog(m_window.get());
        dialog.showPage(page);
        dialog.show();
        if (!QTest::qWaitForWindowExposed(&dialog)) std::exit(5);
        wait(300);
        save(dialog.grab().toImage(), dir + names[page]);
        dialog.close();
    }
}

// manual/getting-started: the manual's window, opened with "?" on a map (FEAT-092).
void Media::helpShots() {
    startWindow(kShotSize);
    open(m_mapPath);
    fit();
    if (auto *button = m_window->findChild<QToolButton *>(QStringLiteral("HelpButton"))) button->click();
    ManualWindow *manual = m_window->manualWindow();
    if (!manual || !QTest::qWaitForWindowExposed(manual)) std::exit(5);
    manual->resize(1280, 820);
    wait(600);
    save(manual->grab().toImage(), QStringLiteral("manual/images/getting-started/manual-window.png"));
    manual->close();
    wait(200);
}

// manual/getting-started: the guided tour's first steps and a tip (FEAT-093). Last: it leaves the
// practice map open with changes.
void Media::tourShots() {
    startWindow(kShotSize);
    GuidedTour *tour = m_window->tour();
    tour->start();
    wait(400);
    tour->next();
    wait(400);
    auto *view = m_window->findChild<MindMapView *>();
    view->setFocus();
    QTest::keyClick(view, Qt::Key_F2);
    QTest::keyClicks(view, QStringLiteral("Holidays"));
    QTest::keyClick(view, Qt::Key_Return);
    wait(400);
    tour->refresh();
    wait(200);
    save(m_window->grab().toImage(), QStringLiteral("manual/images/getting-started/tour-step.png"));
    tour->stop();
    wait(200);
    if (auto *panel = m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"))) panel->click();
    wait(500);
    m_window->showTip(QStringLiteral("pitch"));
    wait(200);
    if (Tips::current()) save(m_window->grab().toImage(), QStringLiteral("manual/images/getting-started/tour-tip.png"));
}

void Media::run(const QStringList &only) {
    const auto wanted = [&only](const char *name) {
        return only.isEmpty() || only.contains(QString::fromLatin1(name));
    };
    buildFiles();
    if (wanted("map")) mapShots(false);
    if (wanted("dark")) mapShots(true);
    if (wanted("outliner")) outlinerShot();
    if (wanted("gantt")) ganttShot();
    if (wanted("pitch")) pitchShot();
    if (wanted("home")) homeShot();
    if (wanted("build")) buildGif();
    if (wanted("structures")) structuresGif();
    if (wanted("themes")) themesGif();
    if (wanted("window")) windowShot();
    if (wanted("updates")) updatesShot();
    if (wanted("basics")) mapBasicsShots();
    if (wanted("styling")) stylingShots();
    if (wanted("contents")) contentsShots();
    if (wanted("outliner-pages")) outlinerShots();
    if (wanted("gantt-pages")) ganttShots();
    if (wanted("pitch-pages")) pitchShots();
    if (wanted("files")) filesShots();
    if (wanted("faster")) fasterShots();
    if (wanted("settings")) settingsShots();
    if (wanted("help")) helpShots();
    if (wanted("tour")) tourShots();
}

} // namespace

int main(int argc, char *argv[]) {
    // Before anything reads QSettings or a standard path: test mode and an organization of its own.
    QStandardPaths::setTestModeEnabled(true);
    std::setvbuf(stdout, nullptr, _IONBF, 0); // progress lines appear at once, also when redirected
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MentisMedia"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisReadmeMedia"));
    {
        QSettings settings; // only this tool's own settings: every run starts the same
        settings.clear();
        settings.setValue(QStringLiteral("ui/language"), QStringLiteral("en")); // FEAT-046; no translator is installed
        settings.setValue(QStringLiteral("gantt/critical"), false);             // no red halo on every task
    }
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom)); // month names and dates
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    NodeItem::loadInterfaceFont();
    QFont font = QApplication::font();
    font.setFamily(NodeStyle::kDefaultMapFontFamily);
    QApplication::setFont(font);
    UiTheme::loadAppearance();
    Performance::setMode(Performance::Mode::Light); // no animations: each frame shows a settled state

    const QStringList arguments = app.arguments();
    if (arguments.size() < 2) {
        std::fprintf(stderr, "usage: MentisReadmeMedia <repository folder> [map,outliner,...]\n");
        return 2;
    }
    QDir().mkpath(arguments[1]);
    Media media(QFileInfo(arguments[1]).absoluteFilePath());
    media.run(arguments.size() > 2 ? arguments[2].split(QLatin1Char(',')) : QStringList());
    return 0;
}
