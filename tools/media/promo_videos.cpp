// Dev tool (MENTIS_BUILD_MEDIA): renders short promotional videos (MP4, H.264, 30 fps, AAC music) of
// Mentis from the real MainWindow on the offscreen platform, in landscape (1920x1080) and portrait
// (1080x1920). The window is driven with real mouse and key events; a drawn cursor, click ripples and
// glows, key badges, captions and a zooming camera are painted over each frame; the music is composed
// here. Frames and sound are encoded with Qt Multimedia (QVideoFrameInput, QAudioBufferInput,
// QMediaRecorder, FFmpeg backend), so no external encoder is needed.
// Settings and files are isolated as in readme_media.cpp: the user's own settings are never touched.
//
// Usage: QT_QPA_PLATFORM=offscreen MentisPromoVideos <output folder> [only]
//   only = a comma-separated list of: map,outliner,gantt,export (each in both formats), or with a
//          suffix -h / -v for one format (map-h, gantt-v, ...); or "music" for the built-in music alone (WAV).
//   --music=builtin (default), none (no sound track) or <sound file>: the videos' music.
#include "document.h"
#include "ganttview.h"
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
#include "uitheme.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QAudioBuffer>
#include <QAudioBufferInput>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLinearGradient>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoFrameInput>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace {

[[noreturn]] void fail(const QString &message) {
    std::fprintf(stderr, "%s\n", qPrintable(message));
    std::exit(3);
}

// ---------------------------------------------------------------------------------------------
// Background music. Where it comes from is chosen with --music (see main): the tune composed below,
// a sound file of the user's, or none. Every source gives 48 kHz stereo; the writer fades it.
constexpr int kMusicRate = 48000;

class MusicSource {
  public:
    virtual ~MusicSource() = default;
    // frames stereo samples, interleaved left/right, in -1..1.
    virtual void render(float *out, int frames) = 0;
};
// A new source for each video (it starts from the beginning); empty: videos without sound.
using MusicFactory = std::function<std::unique_ptr<MusicSource>()>;

// Composed here so the videos carry no third-party rights: a calm lo-fi loop in C major at 88 BPM
// (Fmaj7, Em7, Dm7, Cmaj7, one chord a bar): a soft pad, a plucked arpeggio with a ping-pong echo, a
// round bass, and from the second bar a gentle kick and shaker. Deterministic: the same video
// always gets the same sound.
class Music final : public MusicSource {
  public:
    void render(float *out, int frames) override {
        for (int i = 0; i < frames; ++i, ++m_sample) {
            const double t = m_sample / double(kMusicRate);
            double left = 0, right = 0;
            voice(t, left, right);
            // Ping-pong echo of the pluck, three eighths late.
            const int delay = int(kMusicRate * kBeat * 0.75);
            if (m_echoL.size() != std::size_t(delay)) {
                m_echoL.assign(delay, 0.f);
                m_echoR.assign(delay, 0.f);
            }
            const float echoL = m_echoL[m_echoAt], echoR = m_echoR[m_echoAt];
            m_echoL[m_echoAt] = float(m_pluck * 0.6 + echoR * 0.38);
            m_echoR[m_echoAt] = float(echoL * 0.38);
            m_echoAt = (m_echoAt + 1) % delay;
            left += echoL * 0.55;
            right += echoR * 0.55;
            out[2 * i] = float(std::tanh(left * 2.2) * 0.8);  // about -20 dBFS RMS: background level
            out[2 * i + 1] = float(std::tanh(right * 2.2) * 0.8);
        }
    }

  private:
    static constexpr double kBeat = 60.0 / 88;
    static constexpr double kPi = 3.14159265358979323846;

    static double freq(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }
    static double sine(double f, double t) { return std::sin(2 * kPi * f * t); }

    void voice(double t, double &left, double &right) {
        // MIDI notes of the four chords: Fmaj7, Em7, Dm7, Cmaj7.
        static const int chords[4][4] = {{53, 57, 60, 64}, {52, 55, 59, 62}, {50, 53, 57, 60}, {48, 52, 55, 59}};
        const double bar = 4 * kBeat;
        const int barIndex = int(t / bar);
        const int *chord = chords[barIndex % 4];
        const double inBar = t - barIndex * bar;
        // Pad: two detuned sines a note, a slow swell on each bar, crossfaded into the next chord.
        const double swell = 0.75 + 0.25 * std::sin(2 * kPi * t / (2 * bar));
        const double attack = std::min(1.0, inBar / 0.35);
        for (int n = 0; n < 4; ++n) {
            const double f = freq(chord[n]);
            const double tone = (sine(f * 0.998, t) + sine(f * 1.003, t) + 0.25 * sine(2 * f, t)) * 0.5;
            const double pan = 0.35 + 0.1 * n;
            left += tone * 0.032 * swell * attack * (1 - pan) * 2;
            right += tone * 0.032 * swell * attack * pan * 2;
        }
        // Pluck: eighth notes up and down the chord, an octave higher; exponential decay.
        static const int pattern[8] = {0, 1, 2, 3, 2, 1, 2, 3};
        const double eighth = kBeat / 2;
        const int step = int(t / eighth);
        m_pluck = 0;
        for (int k = std::max(0, step - 6); k <= step; ++k) {
            const double age = t - k * eighth;
            const int kBar = int(k * eighth / bar);
            const double f = freq(chords[kBar % 4][pattern[k % 8]] + 12);
            const double env = std::min(1.0, age / 0.004) * std::exp(-age / 0.22);
            m_pluck += (sine(f, age) + 0.3 * sine(2 * f, age) * std::exp(-age / 0.08)) * env * 0.075;
        }
        left += m_pluck * 0.9;
        right += m_pluck * 0.7;
        // Bass: the root on beats 1 and 3.
        const double half = 2 * kBeat;
        const double bassAge = std::fmod(t, half);
        const double bass = sine(freq(chord[0] - 12), bassAge) * std::exp(-bassAge / 0.6) * 0.12;
        left += bass;
        right += bass;
        if (barIndex < 1) return;  // the first bar without drums
        // Kick on 1 and 3: a short falling sine.
        const double kickAge = bassAge;
        const double kickFreq = 50 + 70 * std::exp(-kickAge / 0.03);
        const double kick = std::sin(2 * kPi * kickFreq * kickAge) * std::exp(-kickAge / 0.12) * 0.16;
        // Shaker on the off-beats: filtered noise from a small hash.
        const double offAge = std::fmod(t + kBeat / 2, kBeat);
        quint32 h = quint32(m_sample) * 2654435761u;
        h ^= h >> 13;
        const double noise = (h % 2001) / 1000.0 - 1;
        m_shaker = m_shaker * 0.6 + noise * 0.4;
        const double shaker = (noise - m_shaker) * std::exp(-offAge / 0.03) * 0.035;
        left += kick + shaker * 0.8;
        right += kick + shaker;
    }

    qint64 m_sample = 0;
    double m_pluck = 0;
    double m_shaker = 0;
    std::vector<float> m_echoL, m_echoR;
    int m_echoAt = 0;
};

// A sound file (MP3, WAV, M4A, ... whatever Qt's FFmpeg backend reads), decoded once to 48 kHz
// stereo and played in a loop when it is shorter than the video.
class FileMusic final : public MusicSource {
  public:
    explicit FileMusic(std::shared_ptr<const std::vector<float>> samples) : m_samples(std::move(samples)) {}

    void render(float *out, int frames) override {
        const std::vector<float> &samples = *m_samples;
        for (int i = 0; i < 2 * frames; ++i) {
            out[i] = samples[m_at];
            m_at = (m_at + 1) % samples.size();
        }
    }

    // The whole file as interleaved stereo at kMusicRate; exits with a message when it cannot.
    static std::shared_ptr<const std::vector<float>> decode(const QString &path) {
        if (!QFileInfo::exists(path)) fail(QStringLiteral("no music file ") + path);
        QAudioFormat wanted;
        wanted.setSampleRate(kMusicRate);
        wanted.setChannelCount(2);
        wanted.setSampleFormat(QAudioFormat::Float);
        QAudioDecoder decoder;
        decoder.setAudioFormat(wanted);
        decoder.setSource(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
        auto samples = std::make_shared<std::vector<float>>();
        bool done = false;
        QObject::connect(&decoder, &QAudioDecoder::bufferReady, [&] { append(*samples, decoder.read()); });
        QObject::connect(&decoder, &QAudioDecoder::finished, [&] { done = true; });
        QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), [&] {
            fail(QStringLiteral("cannot read the music file: ") + decoder.errorString());
        });
        decoder.start();
        QElapsedTimer timer;
        timer.start();
        while (!done && timer.elapsed() < 120000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (samples->size() < 2 * std::size_t(kMusicRate)) fail(QStringLiteral("the music file is shorter than a second"));
        std::printf("music: %s, %.1f s\n", qPrintable(QFileInfo(path).fileName()), samples->size() / 2.0 / kMusicRate);
        return samples;
    }

  private:
    // One decoded buffer, whatever format the backend gave: to stereo float at kMusicRate.
    static void append(std::vector<float> &to, const QAudioBuffer &buffer) {
        const QAudioFormat format = buffer.format();
        const int channels = format.channelCount();
        const qsizetype frames = buffer.frameCount();
        if (channels < 1 || frames <= 1) return;
        const auto sample = [&](qsizetype frame, int channel) -> float {
            const qsizetype i = frame * channels + std::min(channel, channels - 1);
            switch (format.sampleFormat()) {
            case QAudioFormat::Float: return buffer.constData<float>()[i];
            case QAudioFormat::Int16: return buffer.constData<qint16>()[i] / 32768.f;
            case QAudioFormat::Int32: return float(buffer.constData<qint32>()[i] / 2147483648.0);
            case QAudioFormat::UInt8: return (buffer.constData<quint8>()[i] - 128) / 128.f;
            default: return 0.f;
            }
        };
        // Linear resampling when the backend kept the file's own rate.
        const double step = double(format.sampleRate()) / kMusicRate;
        for (double at = 0; at < frames - 1; at += step) {
            const qsizetype frame = qsizetype(at);
            const float mix = float(at - frame);
            for (int channel = 0; channel < 2; ++channel)
                to.push_back(sample(frame, channel) * (1 - mix) + sample(frame + 1, channel) * mix);
        }
    }

    std::shared_ptr<const std::vector<float>> m_samples;
    std::size_t m_at = 0;
};

// ---------------------------------------------------------------------------------------------
// MP4 writer: frames go to a QVideoFrameInput and the music to a QAudioBufferInput; the recorder
// pulls them when it is ready (https://doc.qt.io/qt-6/qvideoframeinput.html,
// https://doc.qt.io/qt-6/qaudiobufferinput.html). Timestamps keep sound and picture together.
class VideoWriter {
  public:
    // music null: a video without a sound track.
    VideoWriter(const QString &path, QSize size, int fps, std::unique_ptr<MusicSource> music)
        : m_fps(fps), m_name(QFileInfo(path).completeBaseName()), m_music(std::move(music)) {
        QVideoFrameFormat format(size, QVideoFrameFormat::Format_BGRX8888);
        format.setStreamFrameRate(fps);
        m_input = std::make_unique<QVideoFrameInput>(format);
        m_audioFormat.setSampleRate(kMusicRate);
        m_audioFormat.setChannelCount(2);
        m_audioFormat.setSampleFormat(QAudioFormat::Int16);
        m_session.setVideoFrameInput(m_input.get());
        if (m_music) {
            m_audio = std::make_unique<QAudioBufferInput>(m_audioFormat);
            m_session.setAudioBufferInput(m_audio.get());
        }
        m_session.setRecorder(&m_recorder);
        QMediaFormat media(QMediaFormat::MPEG4);
        media.setVideoCodec(QMediaFormat::VideoCodec::H264);
        if (m_music) media.setAudioCodec(QMediaFormat::AudioCodec::AAC);
        m_recorder.setMediaFormat(media);
        m_recorder.setVideoResolution(size);
        m_recorder.setVideoFrameRate(fps);
        m_recorder.setEncodingMode(QMediaRecorder::AverageBitRateEncoding);
        m_recorder.setVideoBitRate(size.width() * size.height() * 6); // ~12 Mbit/s at 1080p: sharp UI text
        m_recorder.setAudioSampleRate(kMusicRate);
        m_recorder.setAudioChannelCount(2);
        m_recorder.setAudioBitRate(192000);
        QFile::remove(path);
        m_recorder.setOutputLocation(QUrl::fromLocalFile(path));
        QObject::connect(&m_recorder, &QMediaRecorder::errorOccurred, [](QMediaRecorder::Error, const QString &text) {
            fail(QStringLiteral("recorder: ") + text);
        });
        QObject::connect(m_input.get(), &QVideoFrameInput::readyToSendVideoFrame, [this] { m_ready = true; });
        if (m_audio)
            QObject::connect(m_audio.get(), &QAudioBufferInput::readyToSendAudioBuffer, [this] { m_audioReady = true; });
        m_recorder.record();
    }

    // One picture and the sound that goes with it; gain fades the music in and out.
    void add(const QImage &image, double gain) {
        // With MENTIS_MEDIA_DEBUG naming a folder, one frame a second is also saved there.
        static const QString debug = qEnvironmentVariable("MENTIS_MEDIA_DEBUG");
        if (!debug.isEmpty() && m_count % m_fps == 0)
            image.save(QDir(debug).filePath(QStringLiteral("%1-%2.jpg").arg(m_name).arg(m_count / m_fps, 3, 10, QLatin1Char('0'))),
                       "JPG", 85);
        QVideoFrame frame(image.convertToFormat(QImage::Format_RGB32));
        frame.setStartTime(qint64(m_count) * 1000000 / m_fps);
        frame.setEndTime(qint64(m_count + 1) * 1000000 / m_fps);
        send([&] { return m_input->sendVideoFrame(frame); }, m_ready);
        if (!m_music) {
            ++m_count;
            return;
        }

        const int samples = kMusicRate / m_fps;
        std::vector<float> music(2 * samples);
        m_music->render(music.data(), samples);
        QByteArray pcm(int(music.size() * sizeof(qint16)), Qt::Uninitialized);
        auto *data = reinterpret_cast<qint16 *>(pcm.data());
        for (std::size_t i = 0; i < music.size(); ++i) {
            // The gain moves smoothly inside the frame (no clicks).
            const double g = m_gain + (gain - m_gain) * double(i / 2) / samples;
            data[i] = qint16(std::clamp(music[i] * g, -1.0, 1.0) * 32767);
        }
        m_gain = gain;
        const QAudioBuffer buffer(pcm, m_audioFormat, qint64(m_count) * 1000000 / m_fps);
        send([&] { return m_audio->sendAudioBuffer(buffer); }, m_audioReady);
        ++m_count;
    }

    int frames() const { return m_count; }

    void finish() {
        m_recorder.stop();
        QElapsedTimer timer;
        timer.start();
        while (m_recorder.recorderState() != QMediaRecorder::StoppedState && timer.elapsed() < 60000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }

  private:
    // Retries until the input takes the data; the recorder says when it is ready again.
    static void send(const std::function<bool()> &attempt, bool &ready) {
        QElapsedTimer timer;
        timer.start();
        while (!attempt()) {
            ready = false;
            while (!ready && timer.elapsed() < 20000) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            if (!ready) fail(QStringLiteral("the encoder takes no data"));
        }
    }

    int m_fps;
    QString m_name;
    int m_count = 0;
    bool m_ready = false;
    bool m_audioReady = false;
    double m_gain = 0;
    std::unique_ptr<MusicSource> m_music;
    QAudioFormat m_audioFormat;
    QMediaCaptureSession m_session;
    QMediaRecorder m_recorder;
    std::unique_ptr<QVideoFrameInput> m_input;
    std::unique_ptr<QAudioBufferInput> m_audio;
};

// ---------------------------------------------------------------------------------------------
// Brand colours, from media/mentis-banner.png and docs/design/ui-foundations.md (accent).
const QColor kBrandDark(0x05, 0x24, 0x1d);
const QColor kBrandDeep(0x0a, 0x3a, 0x2e);
const QColor kBrandMint(0x6e, 0xe7, 0x9f);
const QColor kAccent(0x70, 0x47, 0xeb);
constexpr int kFps = 30;
// Pace (user, 2026-11-04: faster videos): pauses at this share of their written length.
constexpr double kPace = 0.62;

struct Format {
    QString suffix;      // file name suffix
    QSize out;           // video size in pixels
    QSize window;        // window size in logical pixels
    qreal scale;         // window pixels to video pixels
    QPoint windowAt;     // window's top left in the video
    bool portrait;
    qreal zoom;          // the camera's zoom on an action
};

const Format kLandscape{QStringLiteral("landscape"), QSize(1920, 1080), QSize(1280, 720), 1.5, QPoint(0, 0), false, 1.6};
// Portrait: the window in the middle, a caption band above, a key band below.
// The window cannot be narrower than about 910 px: it is drawn at 1080/910.
const Format kPortrait{QStringLiteral("portrait"), QSize(1080, 1920), QSize(910, 1190), 1080.0 / 910, QPoint(0, 254), true, 1.55};

double easeOutBack(double t) {
    const double c = 1.6;
    return 1 + (c + 1) * std::pow(t - 1, 3) + c * std::pow(t - 1, 2);
}
double easeInOut(double t) { return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2; }

// ---------------------------------------------------------------------------------------------
// The stage: the current picture of the window plus the overlays, written frame by frame. The view
// stays on the whole window; a camera closes in only for work in one small area (typing a topic or
// a row) and goes back out when the action leaves that area (user, 2026-11-04: zooming on every
// click was restless).
class Stage {
  public:
    Stage(MainWindow *window, const Format &format, const QString &path, std::unique_ptr<MusicSource> music)
        : m_window(window), m_format(format), m_writer(path, format.out, kFps, std::move(music)) {
        m_cursor = QPointF(format.window.width() * 0.62, format.window.height() * 0.58);
        m_camera = m_cursor;
        m_icon = QImage(QStringLiteral(":/assets/appicon/mentis-512.png"));
    }

    const Format &format() const { return m_format; }
    bool portrait() const { return m_format.portrait; }

    // Grabs the window again (and any open menu or dialog above it).
    void refresh() {
        QCoreApplication::processEvents();
        // Opening the side panel can widen a narrow window; it goes back to its size when it can.
        if (m_window->size() != m_format.window && m_window->minimumSizeHint().width() <= m_format.window.width()) {
            m_window->resize(m_format.window);
            QCoreApplication::processEvents();
        }
        QImage image = m_window->grab().toImage().convertToFormat(QImage::Format_RGB32);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPoint origin = m_window->geometry().topLeft();
        for (QWidget *top : QApplication::topLevelWidgets()) {
            if (top == m_window || !top->isVisible() || top->inherits("QTipLabel")) continue;
            const QRect box(top->geometry().topLeft() - origin, top->size());
            // A cheap soft shadow: a few rounded rectangles (AGENTS.md, Performance).
            for (int i = 0; i < 6; ++i) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0, 0, 0, 10));
                painter.drawRoundedRect(QRectF(box).adjusted(-i, -i + 3, i, i + 3), 10 + i, 10 + i);
            }
            painter.drawImage(box.topLeft(), top->grab().toImage());
        }
        painter.end();
        m_picture = image;
    }

    // Frames for this many seconds (at the video's pace); overlays keep moving.
    void hold(double seconds) {
        const int count = std::max(1, int(std::lround(seconds * kPace * kFps)));
        for (int i = 0; i < count; ++i) emitFrame();
    }

    // The cursor glides to a point in window coordinates.
    void moveTo(QPointF target, double seconds = 0.6) {
        const QPointF from = m_cursor;
        const int count = std::max(1, int(std::lround(seconds * 0.72 * kFps)));
        for (int i = 1; i <= count; ++i) {
            m_cursor = from + (target - from) * easeInOut(double(i) / count);
            emitFrame();
        }
    }

    void ripple() { m_ripples.append(m_time); }
    QPointF cursor() const { return m_cursor; }
    // A glow around what was clicked: a rectangle in window coordinates, or a widget it follows
    // (a panel opening right after the click moves the button).
    void glow(const QRectF &box) {
        m_glow = box;
        m_glowWidget.clear();
        m_glowAt = m_time;
    }
    void glow(QWidget *widget) {
        m_glowWidget = widget;
        m_glowAt = m_time;
    }

    // Camera: in on a point where work in a small area follows, or out to the whole window.
    void closeUp(QPointF point) {
        m_focusPoint = point;
        m_targetZoom = m_format.zoom;
    }
    // An action at this point: the camera stays if the point is well inside what it shows, else
    // it goes back to the whole window.
    void keepOrWiden(QPointF point) {
        if (m_targetZoom > 1.001 && !shown().adjusted(40, 40, -40, -40).contains(point)) wide();
    }
    void wide() {
        m_focusPoint.reset();
        m_targetZoom = 1;
    }

    void caption(const QString &text) {
        if (text == m_caption) return;
        m_previousCaption = m_caption;
        m_captionChanged = m_time;
        m_caption = text;
    }
    void keys(const QString &text) {
        m_keys = text;
        m_keysAt = m_time;
    }
    // A light sweep across the window: a new part of the video begins.
    void sweep() { m_sweepAt = m_time; }

    // Title or closing card. The opening one gives way to the app zooming out of it; the app slides
    // away under the closing one, and the music fades out with it.
    void card(const QString &title, const QString &subtitle, double seconds, bool closing) {
        const int count = std::max(1, int(std::lround(seconds * kFps)));
        const int fade = int(0.5 * kFps);
        const QImage cardImage = paintCard(title, subtitle, closing);
        const bool fromApp = closing && !m_picture.isNull();
        const QImage app = fromApp ? composite() : QImage();
        if (closing) m_fadeOutAt = m_time;
        for (int i = 0; i < count; ++i) {
            QImage out = cardImage;
            if (fromApp && i < fade) {
                const double t = easeInOut(double(i) / fade);
                out = QImage(m_format.out, QImage::Format_RGB32);
                QPainter painter(&out);
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                painter.drawImage(0, 0, cardImage);
                // The app shrinks and fades towards the centre.
                const qreal s = 1 - 0.12 * t;
                const QSizeF size = QSizeF(m_format.out) * s;
                painter.setOpacity(1 - t);
                painter.drawImage(QRectF(QPointF((m_format.out.width() - size.width()) / 2,
                                                 (m_format.out.height() - size.height()) / 2),
                                         size),
                                  app);
            }
            write(out);
        }
        m_lastCard = cardImage;
        m_fadeFromCard = !closing;
        m_fadeStart = m_time;
    }

    void finish() {
        m_writer.finish();
        std::printf("  %d frames, %.1f s\n", m_writer.frames(), m_writer.frames() / double(kFps));
    }

    QImage composite() const {
        QImage out(m_format.out, QImage::Format_RGB32);
        QPainter painter(&out);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setRenderHint(QPainter::TextAntialiasing);
        paintBackground(painter);
        const QRectF area = windowArea();
        QPainterPath clip;
        if (m_format.portrait) {
            // Rounded window with a soft shadow on the brand background.
            for (int i = 0; i < 10; ++i) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0, 0, 0, 14));
                painter.drawRoundedRect(area.adjusted(-i * 2, -i * 2 + 8, i * 2, i * 2 + 8), 18 + i, 18 + i);
            }
            clip.addRoundedRect(area, 18, 18);
        } else {
            clip.addRect(area);
        }
        // The window and what happens on it, through the camera.
        painter.save();
        painter.setClipPath(clip);
        painter.setTransform(cameraTransform());
        painter.drawImage(area, m_picture);
        paintGlow(painter);
        paintRipples(painter);
        paintCursor(painter, toVideo(m_cursor));
        paintSweep(painter, area);
        painter.restore();
        paintCaption(painter);
        paintKeys(painter);
        if (m_fadeFromCard && m_time - m_fadeStart < 0.45 && !m_lastCard.isNull()) {
            painter.setOpacity(1 - easeInOut((m_time - m_fadeStart) / 0.45));
            painter.drawImage(0, 0, m_lastCard);
        }
        return out;
    }

  private:
    // Where the window is drawn. Its picture keeps its proportions: a window wider than planned
    // (the side panel can widen it) is drawn smaller, centred.
    QRectF windowArea() const {
        const QSizeF planned = QSizeF(m_format.window) * m_format.scale;
        if (m_picture.isNull()) return QRectF(m_format.windowAt, planned);
        const QSizeF logical = QSizeF(m_picture.size()) / m_picture.devicePixelRatio();
        const qreal fitted = std::min(planned.width() / logical.width(), planned.height() / logical.height());
        const QSizeF drawn = logical * fitted;
        return QRectF(QPointF(m_format.windowAt) +
                          QPointF((planned.width() - drawn.width()) / 2, (planned.height() - drawn.height()) / 2),
                      drawn);
    }
    QPointF toVideo(QPointF window) const {
        const QRectF area = windowArea();
        return area.topLeft() + window * (area.width() / (m_picture.isNull() ? m_format.window.width()
                                                                             : m_picture.width() / m_picture.devicePixelRatio()));
    }

    // The part of the window the camera will show once it settles, in window coordinates.
    QRectF shown() const {
        const QSizeF window = m_window->size();
        const QSizeF view = window / m_targetZoom;
        QPointF centre = m_focusPoint.value_or(m_camera);
        centre.setX(std::clamp(centre.x(), view.width() / 2, window.width() - view.width() / 2));
        centre.setY(std::clamp(centre.y(), view.height() / 2, window.height() - view.height() / 2));
        return QRectF(centre - QPointF(view.width() / 2, view.height() / 2), view);
    }

    // Video coordinates through the camera: zoomed on m_camera, kept inside the window.
    QTransform cameraTransform() const {
        if (m_zoom <= 1.001) return {};
        const QRectF area = windowArea();
        const QSizeF view = area.size() / m_zoom;
        QPointF centre = toVideo(m_camera);
        centre.setX(std::clamp(centre.x(), area.left() + view.width() / 2, area.right() - view.width() / 2));
        centre.setY(std::clamp(centre.y(), area.top() + view.height() / 2, area.bottom() - view.height() / 2));
        QTransform transform;
        transform.translate(area.center().x(), area.center().y());
        transform.scale(m_zoom, m_zoom);
        transform.translate(-centre.x(), -centre.y());
        return transform;
    }

    // The camera eases towards its target each frame (about 0.7 s to settle).
    void moveCamera() {
        m_zoom += (m_targetZoom - m_zoom) * 0.11;
        const QPointF target = m_focusPoint.value_or(m_cursor);
        m_camera += (target - m_camera) * 0.12;
    }

    void write(const QImage &image) {
        // The music fades in over the opening card and out over the closing one.
        double gain = std::min(1.0, m_time / 1.2);
        if (m_fadeOutAt >= 0) gain *= std::max(0.0, 1 - (m_time - m_fadeOutAt) / 2.6);
        m_writer.add(image, gain * 0.85);
        m_time += 1.0 / kFps;
    }

    void emitFrame() {
        if (m_picture.isNull()) refresh();
        moveCamera();
        write(composite());
    }

    void paintBackground(QPainter &painter) const {
        QLinearGradient gradient(0, 0, m_format.out.width() * 0.4, m_format.out.height());
        gradient.setColorAt(0, kBrandDeep);
        gradient.setColorAt(1, kBrandDark);
        painter.fillRect(QRect(QPoint(0, 0), m_format.out), gradient);
    }

    QFont font(int pixels, bool bold) const {
        QFont result = QApplication::font();
        result.setPixelSize(pixels);
        result.setBold(bold);
        return result;
    }

    static void paintCursor(QPainter &painter, QPointF tip) {
        // The usual arrow, 1.35 times the system size so it reads in a video.
        QPainterPath arrow;
        const QPointF points[] = {{0, 0}, {0, 25}, {6.5, 19}, {11, 29}, {15, 27.2}, {10.6, 17.6}, {19, 17.6}};
        arrow.moveTo(points[0]);
        for (const QPointF &point : points) arrow.lineTo(point);
        arrow.closeSubpath();
        painter.save();
        painter.translate(tip);
        painter.scale(1.35, 1.35);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 50));
        painter.drawPath(arrow.translated(1.5, 2));
        painter.setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(QColor(0x14, 0x18, 0x1f));
        painter.drawPath(arrow);
        painter.restore();
    }

    void paintRipples(QPainter &painter) const {
        for (double start : m_ripples) {
            const double age = m_time - start;
            if (age < 0 || age > 0.5) continue;
            const double t = age / 0.5;
            QColor colour = kAccent;
            colour.setAlphaF(0.55 * (1 - t));
            painter.setPen(QPen(colour, 4));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(toVideo(m_cursor), 10 + 34 * t, 10 + 34 * t);
            colour.setAlphaF(0.25 * (1 - t));
            painter.setPen(Qt::NoPen);
            painter.setBrush(colour);
            painter.drawEllipse(toVideo(m_cursor), 12 * (1 - t) + 6, 12 * (1 - t) + 6);
        }
    }

    // A mint glow around the clicked element: in fast, out slowly (0.9 s).
    void paintGlow(QPainter &painter) const {
        const double age = m_time - m_glowAt;
        QRectF window = m_glow;
        if (m_glowWidget) {
            if (!m_glowWidget->isVisible()) return;
            window = QRectF(QPointF(m_glowWidget->mapToGlobal(QPoint(0, 0)) - m_window->mapToGlobal(QPoint(0, 0))),
                            QSizeF(m_glowWidget->size()));
        }
        if (window.isNull() || age < 0 || age > 0.9) return;
        const double strength = std::min(age / 0.1, 1.0) * std::min(1.0, (0.9 - age) / 0.5);
        const QRectF box(toVideo(window.topLeft()), toVideo(window.bottomRight()));
        const qreal grow = 4 + 6 * easeOutBack(std::min(1.0, age / 0.3));
        for (int i = 0; i < 5; ++i) {
            QColor colour = kBrandMint;
            colour.setAlphaF(strength * (i == 0 ? 0.95 : 0.22 / i));
            painter.setPen(QPen(colour, i == 0 ? 3 : 3 + i * 4));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(box.adjusted(-grow, -grow, grow, grow), 10 + grow, 10 + grow);
        }
    }

    // A soft diagonal band of light crossing the window in 0.6 s.
    void paintSweep(QPainter &painter, const QRectF &area) const {
        const double age = m_time - m_sweepAt;
        if (age < 0 || age > 0.6) return;
        const double t = easeInOut(age / 0.6);
        const qreal width = area.width() * 0.35;
        const qreal x = area.left() - width + (area.width() + 2 * width) * t;
        QLinearGradient band(x - width / 2, 0, x + width / 2, area.height() * 0.3);
        QColor mint = kBrandMint;
        mint.setAlphaF(0);
        band.setColorAt(0, mint);
        mint.setAlphaF(0.28);
        band.setColorAt(0.5, mint);
        mint.setAlphaF(0);
        band.setColorAt(1, mint);
        painter.fillRect(area, band);
    }

    // Captions: a pill low in the frame (landscape) or a headline in the top band (portrait). The new
    // one pops in with a little overshoot, the old one drifts up and fades.
    void paintCaption(QPainter &painter) const {
        const double age = m_time - m_captionChanged;
        const double in = std::clamp(age / 0.4, 0.0, 1.0);
        const auto draw = [&](const QString &text, double opacity, double scale, qreal dy) {
            if (text.isEmpty() || opacity <= 0) return;
            painter.save();
            painter.setOpacity(opacity);
            if (m_format.portrait) {
                const QRectF band(60, 40, m_format.out.width() - 120, m_format.windowAt.y() - 60);
                painter.translate(band.center() + QPointF(0, dy));
                painter.scale(scale, scale);
                painter.setFont(font(54, true));
                painter.setPen(Qt::white);
                painter.drawText(band.translated(-band.center()), Qt::AlignCenter | Qt::TextWordWrap, text);
            } else {
                painter.setFont(font(38, true));
                const QFontMetricsF metrics(painter.font());
                const qreal width = metrics.horizontalAdvance(text) + 72;
                const QPointF centre(m_format.out.width() / 2.0, m_format.out.height() - 138 + dy);
                painter.translate(centre);
                painter.scale(scale, scale);
                const QRectF pill(-width / 2, -38, width, 76);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0, 0, 0, 40));
                painter.drawRoundedRect(pill.translated(0, 4), 38, 38);
                painter.setBrush(QColor(kBrandDark.red(), kBrandDark.green(), kBrandDark.blue(), 235));
                painter.drawRoundedRect(pill, 38, 38);
                painter.setBrush(kBrandMint);
                painter.drawEllipse(QPointF(pill.left() + 30, 0), 7, 7);
                painter.setPen(Qt::white);
                painter.drawText(pill.adjusted(28, 0, 0, 0), Qt::AlignCenter, text);
            }
            painter.restore();
        };
        const double out = std::clamp(age / 0.25, 0.0, 1.0);
        draw(m_previousCaption, 1 - out, 1, -24 * out);
        draw(m_caption, std::min(1.0, in * 2), 0.82 + 0.18 * easeOutBack(in), 22 * (1 - easeInOut(in)));
    }

    // The keys just pressed: rounded key caps that pop in, shown 1.2 s.
    void paintKeys(QPainter &painter) const {
        const double age = m_time - m_keysAt;
        if (m_keys.isEmpty() || age > 1.2) return;
        const double opacity = std::min({1.0, age / 0.08, (1.2 - age) / 0.3});
        const double scale = 0.7 + 0.3 * easeOutBack(std::min(1.0, age / 0.25));
        const QStringList caps = m_keys.split(QLatin1Char('+'));
        painter.save();
        painter.setOpacity(opacity);
        painter.setFont(font(m_format.portrait ? 46 : 34, true));
        const QFontMetricsF metrics(painter.font());
        const qreal height = m_format.portrait ? 96 : 72, gap = m_format.portrait ? 44 : 34;
        qreal total = 0;
        for (const QString &cap : caps) total += std::max(height, metrics.horizontalAdvance(cap) + 52) + gap;
        total -= gap;
        const qreal y = m_format.portrait
                            ? m_format.windowAt.y() + m_format.window.height() * m_format.scale +
                                  (m_format.out.height() - m_format.windowAt.y() - m_format.window.height() * m_format.scale - height) / 2
                            : m_format.out.height() - 290;
        painter.translate(m_format.out.width() / 2.0, y + height / 2);
        painter.scale(scale, scale);
        qreal x = -total / 2;
        for (int i = 0; i < caps.size(); ++i) {
            const qreal width = std::max(height, metrics.horizontalAdvance(caps[i]) + 52);
            const QRectF cap(x, -height / 2, width, height);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 70));
            painter.drawRoundedRect(cap.translated(0, 6), 16, 16);
            painter.setBrush(QColor(0xf4, 0xf6, 0xf8));
            painter.drawRoundedRect(cap, 16, 16);
            painter.setPen(QColor(0x14, 0x18, 0x1f));
            painter.drawText(cap.adjusted(0, -3, 0, -3), Qt::AlignCenter, caps[i]);
            x += width;
            if (i + 1 < caps.size()) {
                painter.setPen(Qt::white);
                painter.drawText(QRectF(x, -height / 2, gap, height), Qt::AlignCenter, QStringLiteral("+"));
                x += gap;
            }
        }
        painter.restore();
    }

    QImage paintCard(const QString &title, const QString &subtitle, bool closing) const {
        QImage out(m_format.out, QImage::Format_RGB32);
        QPainter painter(&out);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        paintBackground(painter);
        // The banner's faint arcs.
        painter.setPen(QPen(QColor(kBrandMint.red(), kBrandMint.green(), kBrandMint.blue(), 22), 2));
        for (int i = 0; i < 6; ++i)
            painter.drawEllipse(QPointF(m_format.out.width() * 0.85, m_format.out.height() * 0.95), 300.0 + i * 70,
                                300.0 + i * 70);
        const bool portrait = m_format.portrait;
        const qreal iconSize = portrait ? 340 : 300;
        const QPointF centre(m_format.out.width() / 2.0, m_format.out.height() / 2.0);
        QRectF iconBox, textBox;
        if (portrait) {
            iconBox = QRectF(centre.x() - iconSize / 2, centre.y() - 520, iconSize, iconSize);
            textBox = QRectF(70, iconBox.bottom() + 50, m_format.out.width() - 140, 700);
        } else {
            iconBox = QRectF(centre.x() - 560, centre.y() - iconSize / 2, iconSize, iconSize);
            textBox = QRectF(iconBox.right() + 80, centre.y() - 200, 820, 400);
        }
        painter.drawImage(iconBox, m_icon);
        const int align = (portrait ? Qt::AlignHCenter : Qt::AlignLeft) | Qt::AlignTop | Qt::TextWordWrap;
        painter.setPen(QColor(0xf2, 0xff, 0xf7));
        painter.setFont(font(portrait ? 120 : 112, true));
        QRectF used;
        painter.drawText(textBox, align, QStringLiteral("Mentis"), &used);
        qreal y = used.bottom() + 24;
        painter.setPen(QPen(kBrandMint, 6, Qt::SolidLine, Qt::RoundCap));
        const qreal lineLeft = portrait ? centre.x() - 200 : textBox.left() + 4;
        painter.drawLine(QPointF(lineLeft, y), QPointF(lineLeft + 400, y));
        y += 44;
        painter.setPen(Qt::white);
        painter.setFont(font(portrait ? 64 : 54, true));
        painter.drawText(QRectF(textBox.left(), y, textBox.width(), 400), align, title, &used);
        y = used.bottom() + 22;
        painter.setPen(QColor(0xb8, 0xe6, 0xcb));
        painter.setFont(font(portrait ? 40 : 34, false));
        painter.drawText(QRectF(textBox.left(), y, textBox.width(), 400), align, subtitle, &used);
        if (closing) {
            y = used.bottom() + 36;
            painter.setPen(kBrandMint);
            painter.setFont(font(portrait ? 42 : 36, true));
            painter.drawText(QRectF(textBox.left(), y, textBox.width(), 100), align,
                             QStringLiteral("github.com/simozock/Mentis"));
        }
        painter.end();
        return out;
    }

    MainWindow *m_window;
    Format m_format;
    VideoWriter m_writer;
    QImage m_picture;
    QImage m_icon;
    QImage m_lastCard;
    bool m_fadeFromCard = false;
    double m_fadeStart = 0;
    double m_fadeOutAt = -1;
    double m_time = 0;
    QPointF m_cursor;
    QList<double> m_ripples;
    QRectF m_glow;
    QPointer<QWidget> m_glowWidget;
    double m_glowAt = -10;
    double m_sweepAt = -10;
    QString m_caption, m_previousCaption;
    double m_captionChanged = -10;
    QString m_keys;
    double m_keysAt = -10;
    // Camera, in window coordinates.
    QPointF m_camera;
    std::optional<QPointF> m_focusPoint;
    double m_zoom = 1;
    double m_targetZoom = 1;
};

// ---------------------------------------------------------------------------------------------
// Drives the window: the agents' tools write the starting maps, real input does the rest.
class Director {
  public:
    Director(QString out, MusicFactory music) : m_out(std::move(out)), m_music(std::move(music)) {
        m_access.enabled = true;
        m_tools = McpMapTools::tools([this] { return m_access; });
    }

    void run(const QStringList &only);

  private:
    // --- tools and lookup
    QJsonObject call(const QString &tool, QJsonObject args);
    QJsonObject readMap(const QString &path) { return m_map = call(QStringLiteral("read_map"), {{"path", path}}); }
    QString id(const QString &title) const;
    QString file(const QString &name) const { return QDir(m_files.path()).filePath(name); }
    QAction *action(const QString &text) const;
    MindMapScene *scene() const { return m_window->findChild<MindMapScene *>(); }
    MindMapView *view() const { return m_window->findChild<MindMapView *>(); }
    MindMapModel *model() const { return m_window->findChild<MindMapModel *>(); }
    NodeItem *topic(const QString &title) const;
    NodeItem *topicItem(const MindNode *node) const;
    QWidget *widgetWithTip(const QString &start) const;
    // The first visible widget in any window that matches.
    static QWidget *findWidget(const std::function<bool(QWidget *)> &match);
    static QWidget *named(const QString &objectName);
    static QWidget *buttonWithText(const QString &text);
    void wait(int ms = 120) { QTest::qWait(ms); }

    // --- window
    void startWindow(const Format &format);
    void open(const QString &path, const QString &sheet);
    void fit(qreal margin = 60, qreal maxZoom = 1.0);

    // --- acting on the stage
    QPointF centreOf(QWidget *widget) const;
    QPointF centreOf(NodeItem *item) const;
    void clickAt(QPointF window, Qt::KeyboardModifiers modifiers = {}, bool dbl = false);
    void click(QWidget *widget, double seconds = 0.55);
    void click(NodeItem *item, double seconds = 0.55);
    void press(const QString &badge, Qt::Key key, Qt::KeyboardModifiers modifiers = {}, QWidget *target = nullptr);
    void typeText(const QString &text, QWidget *target = nullptr);
    // A caption, then a beat to read it before the action it announces.
    // A new part: a light sweeps across the window. The camera stays where it is: the next action
    // takes it back out if it happens elsewhere.
    void say(const QString &text) {
        m_stage->sweep();
        m_stage->caption(text);
        m_stage->hold(0.7);
    }
    // A mouse drag on target from one window point to another, the cursor moving with it.
    void drag(QWidget *target, QPointF from, QPointF to, double seconds = 0.7);
    // The view glides to show the whole map (zoomed in at most to maxZoom).
    void glideToFit(double seconds = 0.7, qreal margin = 70, qreal maxZoom = 1.0);
    // Tab or Enter on the selected topic, then the title typed into the new topic.
    void addTopic(const QString &key, const QString &title);
    // Clicks a button that opens a menu, then the entries of path (submenus opened on the way).
    // then, if given, runs while the window that the last entry opens is open, and closes it.
    void chooseFromMenu(QWidget *button, const QStringList &path, const std::function<void()> &then = {});
    // Runs body while the modal window opened by start is open; body closes it.
    void whileModal(const std::function<void()> &start, const std::function<void(QDialog *)> &body);

    // --- content
    // --- outliner
    QTreeView *outline() const;
    QModelIndex row(const QString &title) const;
    QPointF rowPoint(const QString &title) const;
    void clickRow(const QString &title);

    QString buildSolarSystem();
    QString buildEssay();
    QString buildPhotosynthesis();
    static QAbstractButton *buttonIn(QWidget *root, const QString &text);
    QString buildThesis();

    // --- videos
    void record(const QString &name, const Format &format, const std::function<void()> &script);
    void mapVideo();
    void outlinerVideo();
    void ganttVideo();
    void exportVideo();
    bool shortCut() const { return m_stage->portrait(); }

    QString m_out;
    MusicFactory m_music;  // empty: videos without sound
    QTemporaryDir m_files;
    McpMapTools::Access m_access;
    QList<McpServer::Tool> m_tools;
    QJsonObject m_map;
    std::unique_ptr<MainWindow> m_window;
    std::unique_ptr<Stage> m_stage;
    QString m_nextOpen;
};

QJsonObject Director::call(const QString &tool, QJsonObject args) {
    for (const McpServer::Tool &candidate : std::as_const(m_tools))
        if (candidate.name == tool) {
            const McpServer::ToolResult result = candidate.call(args);
            if (result.isError) fail(tool + QStringLiteral(" failed: ") + result.text);
            return result.structured;
        }
    fail(QStringLiteral("no tool ") + tool);
}

QString Director::id(const QString &title) const {
    QList<QJsonObject> pending{m_map.value("root").toObject()};
    while (!pending.isEmpty()) {
        const QJsonObject topic = pending.takeFirst();
        if (topic.value("title").toString() == title) return topic.value("id").toString();
        for (const QJsonValue &child : topic.value("children").toArray()) pending.append(child.toObject());
    }
    fail(QStringLiteral("no topic ") + title);
}

QAction *Director::action(const QString &text) const {
    for (auto *candidate : m_window->findChildren<QAction *>())
        if (candidate->text() == text) return candidate;
    fail(QStringLiteral("no action ") + text);
}

NodeItem *Director::topic(const QString &title) const {
    for (QGraphicsItem *item : scene()->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node()->title == title)
            return nodeItem;
    fail(QStringLiteral("no topic item ") + title);
}

NodeItem *Director::topicItem(const MindNode *node) const {
    for (QGraphicsItem *item : scene()->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node() == node) return nodeItem;
    fail(QStringLiteral("no item for the selected topic"));
}

QWidget *Director::widgetWithTip(const QString &start) const {
    for (QWidget *candidate : m_window->findChildren<QWidget *>())
        if (candidate->isVisible() && candidate->toolTip().startsWith(start)) return candidate;
    fail(QStringLiteral("no widget with tooltip ") + start);
}

QWidget *Director::findWidget(const std::function<bool(QWidget *)> &match) {
    for (QWidget *top : QApplication::topLevelWidgets()) {
        if (!top->isVisible()) continue;
        if (match(top)) return top;
        for (QWidget *candidate : top->findChildren<QWidget *>())
            if (candidate->isVisible() && match(candidate)) return candidate;
    }
    return nullptr;
}

QWidget *Director::named(const QString &objectName) {
    QWidget *found = findWidget([&](QWidget *w) { return w->objectName() == objectName; });
    if (!found) fail(QStringLiteral("no widget named ") + objectName);
    return found;
}

QWidget *Director::buttonWithText(const QString &text) {
    QWidget *found = findWidget([&](QWidget *w) {
        auto *button = qobject_cast<QAbstractButton *>(w);
        return button && button->text() == text;
    });
    if (!found) fail(QStringLiteral("no button ") + text);
    return found;
}

void Director::startWindow(const Format &format) {
    if (!m_window) {
        m_window = std::make_unique<MainWindow>();
        m_window->setOpenFilePicker([this] { return m_nextOpen; });
        m_window->move(0, 0);
        m_window->show();
        if (!QTest::qWaitForWindowExposed(m_window.get())) std::exit(5);
    }
    m_window->resize(format.window);
    wait(300);
    std::printf("  window %dx%d (minimum %dx%d)\n", m_window->width(), m_window->height(),
                m_window->minimumSizeHint().width(), m_window->minimumSizeHint().height());
    m_window->activateWindow();
    QTest::qWaitForWindowActive(m_window.get());
}

void Director::open(const QString &path, const QString &sheet) {
    m_nextOpen = path;
    if (m_window->isWindowModified()) action(QStringLiteral("Save"))->trigger();
    action(QStringLiteral("Open…"))->trigger();
    wait(400);
    auto *document = m_window->findChild<Document *>();
    if (!sheet.isEmpty() && document->renameSheet(document->currentSheet(), sheet)) {
        action(QStringLiteral("Save"))->trigger();
        wait(100);
    }
    // The previous video may have left the window in the outliner or the Gantt view.
    if (QWidget *back = findWidget([this](QWidget *w) {
            auto *button = qobject_cast<QAbstractButton *>(w);
            return button && button->window() == m_window.get() && button->text() == QLatin1String("Mind map");
        })) {
        static_cast<QAbstractButton *>(back)->click();
        wait(300);
    }
    scene()->clearSelection();
}

void Director::fit(qreal margin, qreal maxZoom) {
    const QRectF bounds = scene()->itemsBoundingRect().adjusted(-margin, -margin, margin, margin);
    view()->fitInView(bounds, Qt::KeepAspectRatio);
    if (view()->transform().m11() > maxZoom) view()->setTransform(QTransform::fromScale(maxZoom, maxZoom));
    view()->centerOn(bounds.center());
    wait(150);
}

// In window coordinates, also for widgets in popups and dialogs (their own windows).
QPointF Director::centreOf(QWidget *widget) const {
    return QPointF(widget->mapToGlobal(widget->rect().center()) - m_window->mapToGlobal(QPoint(0, 0)));
}

QPointF Director::centreOf(NodeItem *item) const {
    const QPoint inView = view()->mapFromScene(item->sceneBoundingRect().center());
    return QPointF(view()->viewport()->mapTo(m_window.get(), inView));
}

// A real mouse click at a window point, sent to the widget under it.
void Director::clickAt(QPointF point, Qt::KeyboardModifiers modifiers, bool dbl) {
    QWidget *target = m_window->childAt(point.toPoint());
    if (!target) target = m_window.get();
    const QPoint local = target->mapFrom(m_window.get(), point.toPoint());
    m_stage->keepOrWiden(point);
    m_stage->ripple();
    if (dbl) QTest::mouseDClick(target, Qt::LeftButton, modifiers, local);
    else QTest::mouseClick(target, Qt::LeftButton, modifiers, local);
    wait(60);
}

void Director::click(QWidget *widget, double seconds) {
    m_stage->keepOrWiden(centreOf(widget));
    m_stage->moveTo(centreOf(widget), seconds);
    m_stage->hold(0.12);
    m_stage->ripple();
    m_stage->glow(widget);
    const QPointer<QWidget> clicked(widget);
    QTest::mouseClick(widget, Qt::LeftButton, {}, widget->rect().center());
    wait(80);
    m_stage->refresh();
    // A window that grew re-centres itself: the cursor follows the button it pressed.
    if (clicked && clicked->isVisible() && QLineF(centreOf(clicked), m_stage->cursor()).length() > 4)
        m_stage->moveTo(centreOf(clicked), 0.2);
}

void Director::click(NodeItem *item, double seconds) {
    m_stage->keepOrWiden(centreOf(item));
    m_stage->moveTo(centreOf(item), seconds);
    m_stage->hold(0.12);
    const QPolygon box = view()->mapFromScene(item->sceneBoundingRect());
    m_stage->glow(QRectF(QPointF(view()->viewport()->mapTo(m_window.get(), box.boundingRect().topLeft())),
                         QSizeF(box.boundingRect().size())));
    clickAt(centreOf(item));
    m_stage->refresh();
}

void Director::press(const QString &badge, Qt::Key key, Qt::KeyboardModifiers modifiers, QWidget *target) {
    if (!badge.isEmpty()) m_stage->keys(badge);
    QWidget *to = target ? target : QApplication::focusWidget();
    if (!to) to = m_window.get();
    QTest::keyClick(to, key, modifiers);
    wait(60);
    m_stage->refresh();
}

// About 14 characters a second.
void Director::typeText(const QString &text, QWidget *target) {
    for (const QChar character : text) {
        QWidget *to = target ? target : QApplication::focusWidget();
        if (!to) to = m_window.get();
        QTest::keyClicks(to, QString(character));
        QCoreApplication::processEvents();
        m_stage->refresh();
        m_stage->hold(character == QLatin1Char(' ') ? 0.1 : 0.067);
    }
}

void Director::glideToFit(double seconds, qreal margin, qreal maxZoom) {
    m_stage->wide();
    MindMapView *mapView = view();
    const QRectF bounds = scene()->itemsBoundingRect().adjusted(-margin, -margin, margin, margin);
    // In landscape the caption covers the bottom of the view: the map sits in the part above it.
    const qreal captionPx = m_stage->portrait() ? 0 : 110;
    if (m_stage->portrait()) maxZoom *= 1.35;  // a narrow window: larger topics read better on a phone
    const QSizeF port = mapView->viewport()->size() - QSizeF(0, captionPx);
    const qreal target = std::min({maxZoom, port.width() / bounds.width(), port.height() / bounds.height()});
    const qreal from = mapView->transform().m11();
    const QPointF fromCentre = mapView->mapToScene(mapView->viewport()->rect().center());
    const QPointF toCentre = bounds.center() + QPointF(0, captionPx / 2 / target);
    const int count = std::max(1, int(std::lround(seconds * kFps)));
    for (int i = 1; i <= count; ++i) {
        const double t = double(i) / count;
        const double eased = t < 0.5 ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2) / 2;
        const qreal zoom = from * std::pow(target / from, eased);
        mapView->setTransform(QTransform::fromScale(zoom, zoom));
        mapView->centerOn(fromCentre + (toCentre - fromCentre) * eased);
        m_stage->refresh();
        m_stage->hold(1.0 / kFps);
    }
}

void Director::addTopic(const QString &key, const QString &title) {
    press(key, key == QLatin1String("Tab") ? Qt::Key_Tab : Qt::Key_Return, {}, view()->viewport());
    m_stage->hold(0.25);
    MindNode *node = scene()->selectedNode();
    for (QGraphicsItem *item : scene()->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node() == node)
            nodeItem->beginEditing();
    QCoreApplication::processEvents();
    if (node) m_stage->closeUp(centreOf(topicItem(node)));  // the camera on the topic being written
    typeText(title, view()->viewport());
    QTest::keyClick(view()->viewport(), Qt::Key_Return); // ends the editing
    wait(60);
    m_stage->refresh();
    m_stage->hold(0.2);
}

void Director::drag(QWidget *target, QPointF from, QPointF to, double seconds) {
    const auto local = [&](QPointF window) { return target->mapFrom(m_window.get(), window.toPoint()); };
    m_stage->keepOrWiden(from);
    m_stage->keepOrWiden(to);
    m_stage->moveTo(from, 0.5);
    QTest::mouseMove(target, local(from));
    m_stage->hold(0.15);
    m_stage->ripple();
    QTest::mousePress(target, Qt::LeftButton, {}, local(from));
    m_stage->hold(0.1);
    const int count = std::max(2, int(std::lround(seconds * 0.75 * kFps)));
    for (int i = 1; i <= count; ++i) {
        const QPointF point = from + (to - from) * easeInOut(double(i) / count);
        QTest::mouseMove(target, local(point));
        QCoreApplication::processEvents();
        m_stage->refresh();
        m_stage->moveTo(point, 1.0 / kFps);
    }
    QTest::mouseRelease(target, Qt::LeftButton, {}, local(to));
    wait(120);
    m_stage->refresh();
}

void Director::chooseFromMenu(QWidget *button, const QStringList &path, const std::function<void()> &then) {
    bool done = false;
    bool chosen = false;
    wait(300);  // a menu button just closed ignores the next press for a moment
    // The button opens its menu with exec(), which does not return until the menu closes: the
    // entries are chosen from a timer inside the menu's own loop.
    QTimer::singleShot(250, [&] {
        m_stage->wide();  // a menu is large: the whole window
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        for (int tries = 0; !menu && tries < 20; ++tries) {
            wait(50);
            menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        }
        for (int i = 0; i < path.size(); ++i) {
            if (!menu) fail(QStringLiteral("no menu open for ") + path[i]);
            m_stage->refresh();
            m_stage->hold(0.35);
            QAction *found = nullptr;
            for (QAction *candidate : menu->actions())
                if (QString(candidate->text()).remove(QLatin1Char('&')) == path[i]) found = candidate;
            if (!found) fail(QStringLiteral("no menu entry ") + path[i]);
            const QRect box = menu->actionGeometry(found);
            const QPoint local(box.left() + std::min(90, box.width() / 3), box.center().y());
            m_stage->moveTo(QPointF(menu->mapToGlobal(local) - m_window->mapToGlobal(QPoint(0, 0))), 0.45);
            if (found->menu()) {
                // The submenu opens on hover after a short delay, or at once with the Right key.
                QTest::mouseMove(menu, local);
                menu->setActiveAction(found);
                QTest::keyClick(menu, Qt::Key_Right);
                for (int tries = 0; !found->menu()->isVisible() && tries < 20; ++tries) wait(50);
                if (!found->menu()->isVisible()) break;
                menu = found->menu();
                continue;
            }
            menu->setActiveAction(found);
            m_stage->refresh();
            m_stage->hold(0.2);
            m_stage->ripple();
            if (then) QTimer::singleShot(500, then);
            else  // an unexpected message would wait for ever: it is printed and closed
                QTimer::singleShot(1500, [] {
                    if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                        for (auto *label : dialog->findChildren<QLabel *>())
                            std::fprintf(stderr, "unexpected window: %s\n", qPrintable(label->text()));
                        dialog->reject();
                    }
                });
            // As a click on the entry: the menus close, then the entry's command runs (a click
            // sent to the submenu sometimes reached the entry above it).
            while (QWidget *popup = QApplication::activePopupWidget()) popup->close();
            found->trigger();
            chosen = true;
        }
        // Never leave a menu open: its loop would never return.
        while (QWidget *popup = QApplication::activePopupWidget()) popup->close();
        done = true;
    });
    click(button);
    QElapsedTimer timer;
    timer.start();
    while (!done && timer.elapsed() < 20000) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (!done) fail(QStringLiteral("the menu did not open"));
    if (!chosen) fail(QStringLiteral("menu entry not reached: ") + path.join(QStringLiteral(" > ")));
    wait(100);
    m_stage->refresh();
}

void Director::whileModal(const std::function<void()> &start, const std::function<void(QDialog *)> &body) {
    bool ran = false;
    QTimer::singleShot(600, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        ran = true;
        body(dialog);
        if (dialog->isVisible()) dialog->reject();
    });
    start();
    wait(200);
    if (!ran) fail(QStringLiteral("no modal window opened"));
}

void Director::record(const QString &name, const Format &format, const std::function<void()> &script) {
    const QString path = QDir(m_out).filePath(QStringLiteral("Mentis - %1 (%2).mp4").arg(name, format.suffix));
    std::printf("%s\n", qPrintable(path));
    startWindow(format);
    m_stage = std::make_unique<Stage>(m_window.get(), format, path, m_music ? m_music() : nullptr);
    script();
    m_stage->finish();
    m_stage.reset();
}

// ---------------------------------------------------------------------------------------------
// Content.

QString Director::buildSolarSystem() {
    // One file per recording: the map of the previous one is still open (and locked).
    const QString path = file(QStringLiteral("Solar system %1.mentis").arg(m_stage->format().suffix));
    call(QStringLiteral("create_map"), {{"path", path}, {"title", "The Solar System"}});
    return path;
}


// ---------------------------------------------------------------------------------------------
// Videos.

void Director::mapVideo() {
    const bool brief = shortCut();
    const QString path = buildSolarSystem();
    open(path, QStringLiteral("Astronomy"));
    action(QStringLiteral("Actual size"))->trigger();
    action(QStringLiteral("Zoom in"))->trigger();
    view()->centerOn(scene()->itemsBoundingRect().center());
    m_stage->refresh();
    m_stage->card(QStringLiteral("Mind maps: the basics"), QStringLiteral("Free and open source, for students"), 2.0, false);
    say(QStringLiteral("Start from the main topic"));
    m_stage->hold(0.6);
    NodeItem *root = topic(QStringLiteral("The Solar System"));
    click(root);
    m_stage->hold(0.5);

    say(QStringLiteral("Tab adds a subtopic"));
    addTopic(QStringLiteral("Tab"), QStringLiteral("Inner planets"));
    addTopic(QStringLiteral("Tab"), QStringLiteral("Mercury"));
    say(QStringLiteral("Enter adds the next one"));
    addTopic(QStringLiteral("Enter"), QStringLiteral("Venus"));
    addTopic(QStringLiteral("Enter"), QStringLiteral("Earth"));
    glideToFit(0.6, 90, 1.25);
    click(topic(QStringLiteral("The Solar System")));
    addTopic(QStringLiteral("Tab"), QStringLiteral("Outer planets"));
    addTopic(QStringLiteral("Tab"), QStringLiteral("Jupiter"));
    addTopic(QStringLiteral("Enter"), QStringLiteral("Saturn"));
    if (!brief) {
        click(topic(QStringLiteral("The Solar System")));
        addTopic(QStringLiteral("Tab"), QStringLiteral("Small bodies"));
        addTopic(QStringLiteral("Tab"), QStringLiteral("Asteroids"));
        addTopic(QStringLiteral("Enter"), QStringLiteral("Comets"));
    }
    scene()->clearSelection();
    glideToFit(0.7, 90, 1.25);
    m_stage->hold(0.6);

    say(QStringLiteral("Connect ideas with a relationship"));
    click(topic(QStringLiteral("Earth")));
    click(widgetWithTip(QStringLiteral("Create a relationship")));
    m_stage->hold(0.3);
    click(topic(QStringLiteral("Jupiter")), 0.8);
    m_stage->hold(1.0);

    if (!brief) {
        say(QStringLiteral("Draw a boundary around a branch"));
        click(topic(QStringLiteral("Inner planets")));
        click(widgetWithTip(QStringLiteral("Draw a dashed boundary")));
        scene()->clearSelection();
        m_stage->refresh();
        m_stage->hold(1.2);
    }

    say(QStringLiteral("Add markers and stickers"));
    click(topic(QStringLiteral("Earth")));
    click(widgetWithTip(QStringLiteral("Markers and stickers")));
    glideToFit(0.5, 90, 1.1);
    // The panel opens on the page used last: the previous video may have left it on Sticker.
    click(buttonWithText(QStringLiteral("Marker")), 0.4);
    click(widgetWithTip(QStringLiteral("Star")));
    m_stage->hold(0.4);
    if (!brief) {
        click(topic(QStringLiteral("Inner planets")));
        click(widgetWithTip(QStringLiteral("Priority 1")));
        m_stage->hold(0.4);
    }
    click(topic(QStringLiteral("The Solar System")));
    click(buttonWithText(QStringLiteral("Sticker")));
    m_stage->hold(0.3);
    click(widgetWithTip(QStringLiteral("globe")));
    m_stage->hold(0.8);
    click(widgetWithTip(QStringLiteral("Markers and stickers")));  // closes the panel
    scene()->clearSelection();
    glideToFit(0.5, 90, 1.25);
    m_stage->hold(0.5);

    say(QStringLiteral("Change the map type in one click"));
    click(widgetWithTip(QStringLiteral("Show/hide side panel")));
    m_stage->hold(0.5);
    click(named(QStringLiteral("MapTypeSelect")));
    m_stage->hold(0.6);
    using MapStructure::Type;
    const QList<Type> types = brief ? QList<Type>{Type::Timeline}
                                    : QList<Type>{Type::LogicChart, Type::OrgChart, Type::Timeline};
    for (int i = 0; i < types.size(); ++i) {
        if (i > 0) {
            click(named(QStringLiteral("MapTypeSelect")));
            m_stage->hold(0.4);
        }
        QWidget *tile = named(QStringLiteral("MapTypeTile-") + MapVariants::ofType(types[i]).first()->id);
        for (QWidget *parent = tile->parentWidget(); parent; parent = parent->parentWidget())
            if (auto *scroll = qobject_cast<QScrollArea *>(parent)) {
                scroll->ensureWidgetVisible(tile, 0, 40);
                break;
            }
        m_stage->refresh();
        click(tile, 0.7);
        wait(150);
        glideToFit(0.6, 70, 1.25);
        m_stage->hold(1.1);
    }
    click(named(QStringLiteral("MapTypeSelect")));
    m_stage->hold(0.3);
    click(named(QStringLiteral("MapTypeTile-") + MapVariants::ofType(Type::MindMap).first()->id), 0.7);
    wait(150);
    glideToFit(0.5, 70, 1.25);
    say(QStringLiteral("Pick a colour style"));
    for (const char *tile : brief ? QList<const char *>{"Background 5"}
                                  : QList<const char *>{"Background 2", "Background 5", "Background 4"}) {
        click(widgetWithTip(QString::fromLatin1(tile)), 0.5);
        m_stage->hold(1.0);
    }
    click(widgetWithTip(QStringLiteral("Show/hide side panel")));
    m_stage->moveTo(m_stage->cursor() + QPointF(-260, 300), 0.4);
    glideToFit(0.6, 90, 1.25);

    say(QStringLiteral("Fold a branch to focus"));
    click(topic(QStringLiteral("Outer planets")));
    press(QStringLiteral("Ctrl+/"), Qt::Key_Slash, Qt::ControlModifier, view()->viewport());
    m_stage->hold(1.2);
    press(QStringLiteral("Ctrl+/"), Qt::Key_Slash, Qt::ControlModifier, view()->viewport());
    scene()->clearSelection();
    m_stage->refresh();
    m_stage->caption(QString());
    m_stage->hold(1.0);
    m_stage->card(QStringLiteral("Mind maps: the basics"), QStringLiteral("Free and open source, for students"), 3.0,
                  true);
}

QTreeView *Director::outline() const {
    auto *tree = m_window->findChild<QTreeView *>(QStringLiteral("OutlineTree"));
    if (!tree) fail(QStringLiteral("no outliner"));
    return tree;
}

// The row with this title, depth first.
QModelIndex Director::row(const QString &title) const {
    QAbstractItemModel *items = outline()->model();
    QList<QModelIndex> pending{QModelIndex()};
    while (!pending.isEmpty()) {
        const QModelIndex parent = pending.takeFirst();
        for (int r = 0; r < items->rowCount(parent); ++r) {
            const QModelIndex index = items->index(r, 0, parent);
            if (index.data(Qt::EditRole).toString() == title || index.data(Qt::DisplayRole).toString() == title)
                return index;
            pending.append(index);
        }
    }
    fail(QStringLiteral("no row ") + title);
}

// A point on the row's text, in window coordinates.
QPointF Director::rowPoint(const QString &title) const {
    QTreeView *tree = outline();
    const QRect box = tree->visualRect(row(title));
    const QPoint inViewport(box.left() + std::min(box.width() / 3, 90), box.center().y());
    return QPointF(tree->viewport()->mapTo(m_window.get(), inViewport));
}

void Director::clickRow(const QString &title) {
    const QPointF point = rowPoint(title);
    m_stage->closeUp(point);  // a row's title is edited next
    m_stage->moveTo(point, 0.55);
    m_stage->hold(0.1);
    m_stage->ripple();
    QTest::mouseClick(outline()->viewport(), Qt::LeftButton, {},
                      outline()->viewport()->mapFrom(m_window.get(), point.toPoint()));
    wait(120);
    // The click selects the row; the title's field opens as with a second click.
    if (!qobject_cast<QPlainTextEdit *>(QApplication::focusWidget())) {
        outline()->edit(row(title));
        wait(80);
    }
    if (auto *edit = qobject_cast<QPlainTextEdit *>(QApplication::focusWidget())) edit->moveCursor(QTextCursor::End);
    m_stage->refresh();
}

QString Director::buildEssay() {
    const QString path = file(QStringLiteral("Climate essay %1.mentis").arg(m_stage->format().suffix));
    call(QStringLiteral("create_map"), {{"path", path},
                                        {"markdown", QStringLiteral("# Essay: climate change\n"
                                                                    "- Causes\n"
                                                                    "  - Fossil fuels\n"
                                                                    "  - Deforestation\n"
                                                                    "- Effects\n"
                                                                    "  - Rising seas\n"
                                                                    "  - Heatwaves\n")}});
    return path;
}

void Director::outlinerVideo() {
    const bool brief = shortCut();
    const QString title = QStringLiteral("The outliner");
    const QString subtitle = QStringLiteral("Write your map as a list");
    open(buildEssay(), QStringLiteral("English"));
    fit(80, m_stage->portrait() ? 1.5 : 1.3);
    m_stage->refresh();
    m_stage->card(title, subtitle, 2.0, false);
    say(QStringLiteral("Every map is also a list"));
    m_stage->hold(0.8);
    click(buttonWithText(QStringLiteral("Outliner")), 0.8);
    // The top title's field takes its height from the laid-out text: one more resize settles it.
    m_window->resize(m_window->size() + QSize(1, 0));
    wait(80);
    m_window->resize(m_window->size() - QSize(1, 0));
    wait(200);
    m_stage->refresh();
    m_stage->hold(1.0);

    // Enter confirms the title being written, a second Enter adds the next row (as in the app).
    const auto newRow = [this] {
        if (qobject_cast<QPlainTextEdit *>(QApplication::focusWidget())) {
            press(QStringLiteral("Enter"), Qt::Key_Return);
            wait(60);
            m_stage->hold(0.25);
        }
        press(QStringLiteral("Enter"), Qt::Key_Return, {}, outline());
        // The new row's field holds its default title ("Subtopic 3") with the cursor at the start:
        // selected here so that typing replaces it.
        if (auto *edit = qobject_cast<QPlainTextEdit *>(QApplication::focusWidget())) edit->selectAll();
        // In landscape the caption covers the bottom of the view: the row being written stays above it.
        if (!m_stage->portrait()) {
            QTreeView *tree = outline();
            const QRect box = tree->visualRect(tree->currentIndex());
            const int limit = tree->viewport()->height() - 150;
            if (box.bottom() > limit) tree->verticalScrollBar()->setValue(tree->verticalScrollBar()->value() + box.bottom() - limit);
        }
        // The camera on the row being written.
        QTreeView *tree = outline();
        const QRect box = tree->visualRect(tree->currentIndex());
        m_stage->closeUp(QPointF(tree->viewport()->mapTo(m_window.get(), QPoint(box.left() + 140, box.center().y()))));
        m_stage->refresh();
        m_stage->hold(0.2);
    };
    say(QStringLiteral("Enter adds a row"));
    clickRow(QStringLiteral("Heatwaves"));
    m_stage->hold(0.3);
    newRow();
    typeText(QStringLiteral("Melting ice"));
    m_stage->hold(0.3);
    say(QStringLiteral("Tab and Shift+Tab change the level"));
    newRow();
    press(QStringLiteral("Shift+Tab"), Qt::Key_Backtab, Qt::ShiftModifier);
    typeText(QStringLiteral("Solutions"));
    newRow();
    press(QStringLiteral("Tab"), Qt::Key_Tab);
    say(QStringLiteral("Markdown formats as you type"));
    typeText(QStringLiteral("Renewable **energy**"));
    m_stage->hold(0.6);
    newRow();
    typeText(QStringLiteral("Electric ==transport=="));
    m_stage->hold(0.6);
    if (!brief) {
        newRow();
        typeText(QStringLiteral("[ ] Read the IPCC summary"));
        m_stage->hold(0.4);
    }
    press(QStringLiteral("Enter"), Qt::Key_Return);
    m_stage->hold(0.8);

    say(QStringLiteral("Add a note to any row"));
    clickRow(QStringLiteral("Renewable energy"));
    press(QString(), Qt::Key_Return);  // confirms the title unchanged; the row stays selected
    chooseFromMenu(widgetWithTip(QStringLiteral("Add content to the branch")), {QStringLiteral("Note")});
    m_stage->hold(0.4);
    // The note's field opens under the row, with the focus.
    typeText(QStringLiteral("Solar and **wind** are now the cheapest power."));
    m_stage->hold(0.8);
    press(QString(), Qt::Key_Escape);
    m_stage->hold(1.2);

    say(QStringLiteral("Fold the rows you are done with"));
    outline()->scrollToTop();
    m_stage->refresh();
    m_stage->hold(0.3);
    for (const char *branch : {"Causes", "Effects"}) {
        QTreeView *tree = outline();
        const QRect box = tree->visualRect(row(QString::fromLatin1(branch)));
        // The fold triangle of a first-level row.
        const QPointF bullet(tree->viewport()->mapTo(m_window.get(), QPoint(box.left() + 22, box.center().y())));
        m_stage->keepOrWiden(bullet);
        m_stage->moveTo(bullet, 0.5);
        m_stage->hold(0.1);
        clickAt(bullet);
        m_stage->refresh();
        m_stage->hold(0.6);
    }
    m_stage->hold(0.6);

    if (!brief) {
        say(QStringLiteral("Paste a Markdown list from anywhere"));
        QApplication::clipboard()->setText(
            QStringLiteral("- Sources\n  - IPCC report\n  - NASA climate data\n  - Interview notes\n"));
        QTreeView *tree = outline();
        tree->clearSelection();
        tree->setCurrentIndex(QModelIndex());
        tree->setFocus();
        m_stage->refresh();
        m_stage->hold(0.6);
        press(QStringLiteral("Ctrl+V"), Qt::Key_V, Qt::ControlModifier, tree);
        wait(300);
        tree->scrollToBottom();
        m_stage->refresh();
        m_stage->hold(1.6);
    }

    say(QStringLiteral("Switch back: the map is ready"));
    click(buttonWithText(QStringLiteral("Mind map")), 0.8);
    scene()->clearSelection();
    fit(80, 1.0);
    glideToFit(0.6, 80, 1.2);
    m_stage->hold(2.2);
    m_stage->caption(QString());
    m_stage->card(title, subtitle, 3.0, true);
}
// The thesis plan of readme_media.cpp, with a snapshot of the first plan and two people.
QString Director::buildThesis() {
    const QString path = file(QStringLiteral("Thesis plan %1.mentis").arg(m_stage->format().suffix));
    call(QStringLiteral("create_map"), {{"path", path},
                                        {"markdown", QStringLiteral("# Thesis plan\n"
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
                                                                    "  - Submit thesis\n")}});
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
        {"Figures and tables", "2026-11-17", 5, 0},     {"Conclusion", "2026-11-24", 2, 0},
        {"Supervisor feedback", "2026-11-26", 3, 0},    {"Final edit", "2026-11-29", 2, 0},
        {"Submit thesis", "2026-12-01", 1, 0},
    };
    for (const Task &task : tasks)
        call(QStringLiteral("set_gantt_task"), {{"path", path},
                                                {"id", id(QString::fromLatin1(task.title))},
                                                {"start", QString::fromLatin1(task.start)},
                                                {"duration", task.days},
                                                {"progress", task.progress}});
    // Chapters 1-3 -> Conclusion is left out: the video draws it.
    const std::pair<const char *, const char *> links[] = {
        {"Choose the topic", "Read the sources"},     {"Read the sources", "Outline approved"},
        {"Interview two experts", "Outline approved"}, {"Outline approved", "Introduction"},
        {"Introduction", "Chapters 1-3"},             {"Conclusion", "Supervisor feedback"},
        {"Supervisor feedback", "Final edit"},        {"Final edit", "Submit thesis"},
    };
    for (const auto &[from, to] : links)
        call(QStringLiteral("add_dependency"),
             {{"path", path}, {"from", id(QString::fromLatin1(from))}, {"to", id(QString::fromLatin1(to))}});
    call(QStringLiteral("set_task"),
         {{"path", path}, {"ids", QJsonArray{id("Outline approved"), id("Submit thesis")}}, {"state", "todo"}});
    call(QStringLiteral("add_baseline"), {{"path", path}, {"name", "First plan"}});
    const QJsonObject student = call(QStringLiteral("add_person"), {{"path", path},
                                                                     {"first_name", "Ana"},
                                                                     {"last_name", "Silva"},
                                                                     {"role", "Student"},
                                                                     {"hourly_cost", 15}});
    call(QStringLiteral("add_person"), {{"path", path},
                                        {"first_name", "Marco"},
                                        {"last_name", "Rossi"},
                                        {"role", "Supervisor"},
                                        {"hourly_cost", 40}});
    QJsonArray studentTasks;
    for (const char *title : {"Read the sources", "Interview two experts", "Introduction", "Chapters 1-3"})
        studentTasks.append(id(QString::fromLatin1(title)));
    call(QStringLiteral("assign_people"),
         {{"path", path}, {"ids", studentTasks}, {"assign", QJsonArray{student.value("id").toString()}}});
    return path;
}

void Director::ganttVideo() {
    const QString title = QStringLiteral("Gantt: plan your study");
    const QString subtitle = QStringLiteral("Your map becomes a timeline");
    open(buildThesis(), QStringLiteral("Thesis"));
    fit(60, 1.0);
    m_stage->refresh();
    m_stage->card(title, subtitle, 2.0, false);
    say(QStringLiteral("Your map is also a plan"));
    m_stage->hold(1.0);
    click(buttonWithText(QStringLiteral("Gantt")), 0.8);
    wait(400);
    m_stage->refresh();
    const bool brief = shortCut();
    auto *gantt = m_window->findChild<GanttView *>();
    if (!gantt) fail(QStringLiteral("no Gantt view"));
    GanttChart *chart = gantt->chart();
    if (m_stage->portrait()) {
        // A narrow window: the table shows the names only and the month fits the chart, as a user
        // would hide the columns, drag the splitter and zoom out. Columns: 1 Start, 3 Duration
        // (ganttview.cpp); shown again at the end, the setting is the tool's own.
        gantt->setColumnShown(1, false);
        gantt->setColumnShown(3, false);
        if (auto *splitter = m_window->findChild<QSplitter *>(QStringLiteral("GanttSplitter"))) {
            const int total = splitter->sizes().value(0) + splitter->sizes().value(1);
            splitter->setSizes({250, total - 250});
        }
        wait(100);
        const double days = 33;  // 1 November to 3 December
        const int percent = int(100.0 * (chart->viewport()->width() - 10) / (chart->dayWidth() * days) *
                                chart->zoomPercent() / 100.0);
        chart->setZoomPercent(std::clamp(percent, GanttChart::kMinZoom, 100));
        chart->scrollToDay(QDate(2026, 11, 1));
        wait(150);
        m_stage->refresh();
    }
    say(QStringLiteral("Every topic is a task"));
    m_stage->hold(1.6);
    QWidget *port = chart->viewport();
    const auto at = [&](QPointF viewportPoint) { return QPointF(port->mapTo(m_window.get(), viewportPoint.toPoint())); };
    const auto bar = [&](const char *task) { return chart->barRect(id(QString::fromLatin1(task))); };
    const qreal day = chart->dayWidth();

    say(QStringLiteral("Drag a bar to move the task"));
    {
        const QRectF box = bar("Figures and tables");
        drag(port, at(box.center()), at(box.center() + QPointF(2 * day, 0)));
        m_stage->hold(0.8);
    }
    say(QStringLiteral("Drag its end to change the length"));
    {
        const QRectF box = bar("Chapters 1-3");
        const QPointF end(box.right() - 3, box.center().y());
        drag(port, at(end), at(end + QPointF(2 * day, 0)));
        m_stage->hold(0.8);
    }
    say(QStringLiteral("Link two tasks with a drag"));
    {
        const QPointF handle = chart->linkHandle(id(QStringLiteral("Chapters 1-3")));
        m_stage->moveTo(at(bar("Chapters 1-3").center()), 0.5);  // the handle shows on hover
        QTest::mouseMove(port, bar("Chapters 1-3").center().toPoint());
        m_stage->refresh();
        m_stage->hold(0.2);
        drag(port, at(handle), at(bar("Conclusion").center()), 0.9);
        m_stage->hold(1.0);
    }
    if (!brief) {
        say(QStringLiteral("Drag the progress"));
        const QString introduction = id(QStringLiteral("Introduction"));
        const QPointF handle = chart->progressHandle(introduction);
        m_stage->moveTo(at(bar("Introduction").center()), 0.5);
        QTest::mouseMove(port, bar("Introduction").center().toPoint());
        m_stage->refresh();
        drag(port, at(handle), at(QPointF(bar("Introduction").left() + bar("Introduction").width() * 0.8, handle.y())));
        m_stage->hold(0.8);
    }

    say(QStringLiteral("See what sets the end date"));
    click(named(QStringLiteral("ganttCriticalButton")));
    m_stage->hold(2.0);
    click(named(QStringLiteral("ganttCriticalButton")));

    say(QStringLiteral("Compare with your first plan"));
    click(named(QStringLiteral("ganttBaselineButton")));
    m_stage->hold(0.6);
    click(named(QStringLiteral("GanttBaselineName")));
    if (QWidget *menu = findWidget([](QWidget *w) { return w->objectName() == QLatin1String("GanttBaselineMenu"); }))
        menu->hide();
    m_stage->refresh();
    m_stage->hold(2.2);
    gantt->compareBaseline(QString());

    if (!brief) {
        say(QStringLiteral("Dates, people and costs in the side panel"));
        gantt->selectTask(id(QStringLiteral("Chapters 1-3")));
        click(named(QStringLiteral("ganttPanelButton")));
        m_stage->hold(2.2);
        click(named(QStringLiteral("ganttPanelButton")));
    }

    if (!brief) {
        say(QStringLiteral("People, hours and costs"));
        whileModal([this] { click(named(QStringLiteral("ganttResourcesButton"))); },
                   [this](QDialog *dialog) {
                       m_stage->refresh();
                       m_stage->hold(2.4);
                       dialog->reject();
                       wait(100);
                   });
        m_stage->refresh();
    }

    say(QStringLiteral("Days, weeks or months"));
    auto *scale = qobject_cast<QComboBox *>(named(QStringLiteral("ganttScale")));
    m_stage->moveTo(centreOf(scale), 0.6);
    m_stage->ripple();
    m_stage->hold(0.2);
    scale->setCurrentIndex(1);
    wait(200);
    m_stage->refresh();
    m_stage->hold(1.8);
    m_stage->caption(QString());
    m_stage->card(title, subtitle, 3.0, true);
    if (m_stage->portrait()) {
        gantt->setColumnShown(1, true);
        gantt->setColumnShown(3, true);
    }
}
QString Director::buildPhotosynthesis() {
    const QString path = file(QStringLiteral("Photosynthesis %1.mentis").arg(m_stage->format().suffix));
    call(QStringLiteral("create_map"), {{"path", path},
                                        {"markdown", QStringLiteral("# Photosynthesis\n"
                                                                    "- Light reactions\n"
                                                                    "  - In the thylakoids\n"
                                                                    "  - Water is split\n"
                                                                    "- Calvin cycle\n"
                                                                    "  - In the stroma\n"
                                                                    "  - Builds glucose\n"
                                                                    "- Inputs\n"
                                                                    "  - Sunlight\n"
                                                                    "  - Water\n"
                                                                    "  - Carbon dioxide\n"
                                                                    "- Outputs\n"
                                                                    "  - Glucose\n"
                                                                    "  - Oxygen\n")}});
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
    call(QStringLiteral("add_boundary"),
         {{"path", path}, {"ids", QJsonArray{id("Sunlight"), id("Water"), id("Carbon dioxide")}}, {"title", "Raw materials"}});
    return path;
}

QAbstractButton *Director::buttonIn(QWidget *root, const QString &text) {
    for (auto *button : root->findChildren<QAbstractButton *>())
        if (button->isVisible() && QString(button->text()).remove(QLatin1Char('&')) == text) return button;
    fail(QStringLiteral("no button ") + text);
}

void Director::exportVideo() {
    const bool brief = shortCut();
    const QString title = QStringLiteral("Export your work");
    const QString subtitle = QStringLiteral("PDF, image, Word, Excel, PowerPoint, Markdown");
    open(buildPhotosynthesis(), QStringLiteral("Biology"));
    glideToFit(0.05, 60, 1.2);
    m_stage->refresh();
    m_stage->card(title, subtitle, 2.0, false);
    QWidget *mainMenu = widgetWithTip(QStringLiteral("Main menu"));
    const auto modal = [] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) fail(QStringLiteral("no export window"));
        return dialog;
    };

    say(QStringLiteral("Main menu › Export"));
    m_stage->hold(0.6);
    chooseFromMenu(mainMenu, {QStringLiteral("Export"), QStringLiteral("Image…")}, [&] {
        QDialog *dialog = modal();
        m_stage->refresh();
        say(QStringLiteral("A picture of the map: PNG or SVG"));
        m_stage->hold(1.4);
        if (auto *frame = dialog->findChild<QAbstractButton *>(QStringLiteral("ExportFrame"))) {
            say(QStringLiteral("Add a frame if you like"));
            click(frame);
            m_stage->hold(1.6);
        }
        click(dialog->findChild<QAbstractButton *>(QStringLiteral("ModalClose")));
    });
    m_stage->hold(0.4);

    chooseFromMenu(mainMenu, {QStringLiteral("Export"), QStringLiteral("PDF…")}, [&] {
        QDialog *dialog = modal();
        m_stage->refresh();
        say(QStringLiteral("A PDF of the map…"));
        m_stage->hold(1.2);
        click(buttonIn(dialog, QStringLiteral("Landscape")));
        m_stage->hold(1.0);
        say(QStringLiteral("…of the outline, or of the slides"));
        click(buttonIn(dialog, QStringLiteral("Outliner")));
        m_stage->hold(1.4);
        if (!brief) {
            click(buttonIn(dialog, QStringLiteral("Pitch")));
            m_stage->hold(1.6);
        }
        click(buttonIn(dialog, QStringLiteral("Cancel")));
    });
    m_stage->hold(0.4);

    // Word, Excel, Pitch and Markdown ask only for a file name: here a file in the tool's folder.
    say(QStringLiteral("Word, Excel, PowerPoint and Markdown too"));
    const QStringList formats = brief ? QStringList{QStringLiteral("Word…"), QStringLiteral("Pitch…")}
                                      : QStringList{QStringLiteral("Word…"), QStringLiteral("Excel…"),
                                                    QStringLiteral("Pitch…"), QStringLiteral("Markdown…")};
    // The status bar says "Exported to <full path>": the video shows only the file's name.
    QStatusBar *bar = m_window->statusBar();
    const QString folder = QDir::toNativeSeparators(m_files.path()) + QDir::separator();
    const QMetaObject::Connection shortPath =
        QObject::connect(bar, &QStatusBar::messageChanged, bar, [bar, folder](const QString &message) {
            if (message.contains(folder)) bar->showMessage(QString(message).remove(folder), 6000);
        });
    for (const QString &format : formats) {
        const QString suffix = format == QLatin1String("Word…")    ? QStringLiteral(".docx")
                               : format == QLatin1String("Excel…") ? QStringLiteral(".xlsx")
                               : format == QLatin1String("Pitch…") ? QStringLiteral(".pptx")
                                                                    : QStringLiteral(".md");
        m_window->setExportFilePicker([this, suffix] { return file(QStringLiteral("Photosynthesis") + suffix); });
        // Word, Excel and Markdown first show what is exported; Pitch saves at once.
        chooseFromMenu(mainMenu, {QStringLiteral("Export"), format}, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            m_stage->refresh();
            m_stage->hold(1.0);
            click(dialog->findChild<QAbstractButton *>(QStringLiteral("ModalAccept")));
        });
        m_stage->refresh();
        m_stage->hold(0.9);
    }
    QObject::disconnect(shortPath);
    m_stage->hold(0.8);
    m_stage->caption(QString());
    m_stage->card(title, subtitle, 3.0, true);
}

void Director::run(const QStringList &only) {
    const auto wanted = [&only](const QString &name, const Format &format) {
        if (only.isEmpty()) return true;
        const QString suffix = format.portrait ? QStringLiteral("-v") : QStringLiteral("-h");
        return only.contains(name) || only.contains(name + suffix);
    };
    struct Video {
        QString key, title;
        void (Director::*script)();
    };
    const Video videos[] = {
        {QStringLiteral("map"), QStringLiteral("Mind map basics"), &Director::mapVideo},
        {QStringLiteral("outliner"), QStringLiteral("Outliner"), &Director::outlinerVideo},
        {QStringLiteral("gantt"), QStringLiteral("Gantt basics"), &Director::ganttVideo},
        {QStringLiteral("export"), QStringLiteral("Export"), &Director::exportVideo},
    };
    for (const Format *format : {&kLandscape, &kPortrait})
        for (const Video &video : videos)
            if (wanted(video.key, *format)) record(video.title, *format, [this, &video] { (this->*video.script)(); });
}

// The music alone, as a 16-bit stereo WAV (argument "music"): to listen to it and check its level.
void writeMusic(const QString &path, int seconds) {
    Music music;
    const int frames = kMusicRate * seconds;
    std::vector<float> samples(2 * std::size_t(frames));
    music.render(samples.data(), frames);
    float peak = 0;
    double sum = 0;
    QByteArray pcm;
    pcm.reserve(int(samples.size() * 2));
    for (float sample : samples) {
        peak = std::max(peak, std::abs(sample));
        sum += double(sample) * sample;
        const qint16 value = qint16(std::clamp(sample * 0.85f, -1.f, 1.f) * 32767);
        pcm.append(char(value & 0xff));
        pcm.append(char((value >> 8) & 0xff));
    }
    QByteArray wav("RIFF");
    const auto u32 = [&wav](quint32 v) { for (int i = 0; i < 4; ++i) wav.append(char((v >> (8 * i)) & 0xff)); };
    const auto u16 = [&wav](quint16 v) { wav.append(char(v & 0xff)); wav.append(char(v >> 8)); };
    u32(36 + pcm.size());
    wav.append("WAVEfmt ");
    u32(16); u16(1); u16(2); u32(kMusicRate); u32(kMusicRate * 4); u16(4); u16(16);
    wav.append("data");
    u32(pcm.size());
    wav.append(pcm);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(wav) != wav.size()) fail(QStringLiteral("cannot write ") + path);
    std::printf("%s: peak %.2f, rms %.3f\n", qPrintable(path), peak, std::sqrt(sum / samples.size()));
}

} // namespace

int main(int argc, char *argv[]) {
    QStandardPaths::setTestModeEnabled(true);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // The window is drawn at 1.5x: sharp UI text in a 1080p video.
    qputenv("QT_SCALE_FACTOR", "1.5");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MentisMedia"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisPromoVideos"));
    {
        QSettings settings;
        settings.clear();
        settings.setValue(QStringLiteral("ui/language"), QStringLiteral("en"));
        settings.setValue(QStringLiteral("gantt/critical"), false);
        settings.setValue(QStringLiteral("tour/offered"), true); // no tour offers or tips over the videos
        settings.setValue(QStringLiteral("tour/tips"), false);
    }
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    NodeItem::loadInterfaceFont();
    QFont font = QApplication::font();
    font.setFamily(NodeStyle::kDefaultMapFontFamily);
    QApplication::setFont(font);
    UiTheme::loadAppearance();
    Performance::setMode(Performance::Mode::Light);

    // --music=builtin (default: the tune composed above), --music=none (no sound track) or
    // --music=<sound file>: taken out of the arguments, anywhere among them.
    QStringList arguments = app.arguments();
    QString musicChoice = QStringLiteral("builtin");
    for (int i = arguments.size() - 1; i > 0; --i)
        if (arguments[i].startsWith(QLatin1String("--music="))) musicChoice = arguments.takeAt(i).mid(8);
    if (arguments.size() < 2) {
        std::fprintf(stderr, "usage: MentisPromoVideos <output folder> [map,outliner,gantt,export|music] [--music=builtin|none|<file>]\n");
        return 2;
    }
    QDir().mkpath(arguments[1]);
    if (arguments.size() > 2 && arguments[2] == QLatin1String("music")) {
        writeMusic(QDir(arguments[1]).filePath(QStringLiteral("Mentis - music.wav")), 40);
        return 0;
    }
    MusicFactory music;
    if (musicChoice == QLatin1String("builtin")) {
        music = [] { return std::make_unique<Music>(); };
    } else if (musicChoice != QLatin1String("none")) {
        const auto samples = FileMusic::decode(musicChoice);
        music = [samples] { return std::make_unique<FileMusic>(samples); };
    }
    Director director(QFileInfo(arguments[1]).absoluteFilePath(), music);
    director.run(arguments.size() > 2 ? arguments[2].split(QLatin1Char(','), Qt::SkipEmptyParts) : QStringList());
    return 0;
}
