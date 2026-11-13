#include "incomingmaps.h"

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#include <QPointer>
#include <QtCore/private/qjnihelpers_p.h>

namespace {

// The map's URI of a VIEW or SEND intent; empty for any other intent (the launcher's MAIN).
QUrl mapUrlOf(const QJniObject &intent) {
    if (!intent.isValid()) return {};
    const QString action = intent.callObjectMethod("getAction", "()Ljava/lang/String;").toString();
    QJniObject uri;
    if (action == QLatin1String("android.intent.action.VIEW")) {
        uri = intent.callObjectMethod("getData", "()Landroid/net/Uri;");
    } else if (action == QLatin1String("android.intent.action.SEND")) {
        // getParcelableExtra(String) is deprecated from API 33 for a typed overload that API 28-32
        // lack; it still works (https://developer.android.com/reference/android/content/Intent#getParcelableExtra(java.lang.String)).
        uri = intent.callObjectMethod("getParcelableExtra", "(Ljava/lang/String;)Landroid/os/Parcelable;",
                                      QJniObject::fromString(QStringLiteral("android.intent.extra.STREAM")).object<jstring>());
    }
    if (!uri.isValid()) return {};
    return QUrl(uri.callObjectMethod("toString", "()Ljava/lang/String;").toString());
}

void deliver(const QPointer<IncomingMaps> &target, const QUrl &url) {
    if (url.isEmpty()) return;
    QMetaObject::invokeMethod(QCoreApplication::instance(), [target, url] {
        if (target) emit target->received(url);
    }, Qt::QueuedConnection);
}

}  // namespace

// onNewIntent arrives on Android's thread: the URI is handed to the GUI thread.
class IncomingMaps::Listener : public QtAndroidPrivate::NewIntentListener {
public:
    explicit Listener(IncomingMaps *owner) : m_owner(owner) {}
    bool handleNewIntent(JNIEnv *, jobject intent) override {
        const QUrl url = mapUrlOf(QJniObject(intent));
        deliver(m_owner, url);
        return !url.isEmpty();
    }

private:
    QPointer<IncomingMaps> m_owner;
};

IncomingMaps::IncomingMaps(QObject *parent) : QObject(parent), m_listener(std::make_unique<Listener>(this)) {
    QtAndroidPrivate::registerNewIntentListener(m_listener.get());
    const QJniObject activity(QtAndroidPrivate::activity().object());
    if (activity.isValid()) deliver(this, mapUrlOf(activity.callObjectMethod("getIntent", "()Landroid/content/Intent;")));
}

IncomingMaps::~IncomingMaps() { QtAndroidPrivate::unregisterNewIntentListener(m_listener.get()); }

#else

class IncomingMaps::Listener {};

IncomingMaps::IncomingMaps(QObject *parent) : QObject(parent) {}

IncomingMaps::~IncomingMaps() = default;

#endif
