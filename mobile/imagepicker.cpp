#include "imagepicker.h"

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#include <QPointer>
#include <QtCore/private/qandroidextras_p.h>

namespace {

constexpr int kRequestCode = 0x4d49;  // "MI": this app's only activity request
constexpr jint kResultOk = -1;        // android.app.Activity.RESULT_OK
constexpr jint kPhotoPickerSdk = 33;  // Android 13: MediaStore.ACTION_PICK_IMAGES

QJniObject intentFor(const char *action) {
    return QJniObject("android/content/Intent", "(Ljava/lang/String;)V", QJniObject::fromString(QString::fromLatin1(action)).object<jstring>());
}

}  // namespace

bool ImagePicker::open() {
    const jint sdk = QJniObject::getStaticField<jint>("android/os/Build$VERSION", "SDK_INT");
    QJniObject intent;
    if (sdk >= kPhotoPickerSdk) {
        intent = intentFor("android.provider.action.PICK_IMAGES");
    } else {
        intent = intentFor("android.intent.action.GET_CONTENT");
        intent.callObjectMethod("setType", "(Ljava/lang/String;)Landroid/content/Intent;", QJniObject::fromString(QStringLiteral("image/*")).object<jstring>());
        intent.callObjectMethod("addCategory", "(Ljava/lang/String;)Landroid/content/Intent;",
                                QJniObject::fromString(QStringLiteral("android.intent.category.OPENABLE")).object<jstring>());
    }
    if (!intent.isValid()) return false;
    QPointer<ImagePicker> self(this);
    // The result comes on Android's thread: it is handed to the GUI thread, where the picker may
    // already be gone.
    QtAndroidPrivate::startActivity(intent, kRequestCode, [self](int, int resultCode, const QJniObject &data) {
        QUrl url;
        if (resultCode == kResultOk && data.isValid())
            if (const QJniObject uri = data.callObjectMethod("getData", "()Landroid/net/Uri;"); uri.isValid())
                url = QUrl(uri.callObjectMethod("toString", "()Ljava/lang/String;").toString());
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, url] {
            if (!self) return;
            if (url.isEmpty()) emit self->canceled();
            else emit self->picked(url);
        }, Qt::QueuedConnection);
    });
    return true;
}

#else

bool ImagePicker::open() { return false; }

#endif
