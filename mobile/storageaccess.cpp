#include "storageaccess.h"

#include "mapfolder.h"

#include <QSettings>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#include <QPointer>
#include <QtCore/private/qandroidextras_p.h>
#endif

namespace {

const QString kDeclinedKey = QStringLiteral("mentisFolderAccessDeclined");

}  // namespace

StorageAccess::StorageAccess(QObject *parent) : QObject(parent) {
#ifdef Q_OS_ANDROID
    m_needed = !MapFolder::sharedStorageAllowed();
#endif
}

bool StorageAccess::declined() const { return QSettings().value(kDeclinedKey, false).toBool(); }

void StorageAccess::decline() {
    QSettings().setValue(kDeclinedKey, true);
    emit changed();
}

void StorageAccess::recheck() {
#ifdef Q_OS_ANDROID
    const bool needed = !MapFolder::sharedStorageAllowed();
    if (needed == m_needed) return;
    m_needed = needed;
    emit changed();
    if (!needed) emit granted();
#endif
}

#ifdef Q_OS_ANDROID

namespace {

constexpr int kRequestCode = 0x4d46;  // "MF"
constexpr jint kAllFilesAccessSdk = 30;

QJniObject settingsIntent(const char *action, const QJniObject &data) {
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V", QJniObject::fromString(QString::fromLatin1(action)).object<jstring>());
    if (data.isValid()) intent.callObjectMethod("setData", "(Landroid/net/Uri;)Landroid/content/Intent;", data.object());
    return intent;
}

}  // namespace

void StorageAccess::request() {
    QPointer<StorageAccess> self(this);
    const auto recheckLater = [self] {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self] { if (self) self->recheck(); }, Qt::QueuedConnection);
    };
    if (QJniObject::getStaticField<jint>("android/os/Build$VERSION", "SDK_INT") >= kAllFilesAccessSdk) {
        const QJniObject context(QNativeInterface::QAndroidApplication::context().object());
        const QString package = context.callObjectMethod("getPackageName", "()Ljava/lang/String;").toString();
        const QJniObject uri = QJniObject::callStaticObjectMethod("android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                                                                  QJniObject::fromString(QStringLiteral("package:") + package).object<jstring>());
        // The app's own switch; the list of every app where a device has no such page.
        QJniObject intent = settingsIntent("android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION", uri);
        const QJniObject manager = context.callObjectMethod("getPackageManager", "()Landroid/content/pm/PackageManager;");
        if (!intent.callObjectMethod("resolveActivity", "(Landroid/content/pm/PackageManager;)Landroid/content/ComponentName;", manager.object()).isValid())
            intent = settingsIntent("android.settings.MANAGE_ALL_FILES_ACCESS_PERMISSION", QJniObject());
        QtAndroidPrivate::startActivity(intent, kRequestCode, [recheckLater](int, int, const QJniObject &) { recheckLater(); });
        return;
    }
    QtAndroidPrivate::requestPermission(QStringLiteral("android.permission.WRITE_EXTERNAL_STORAGE"))
        .then(this, [this](QtAndroidPrivate::PermissionResult) { recheck(); });
}

#else

void StorageAccess::request() {}

#endif
