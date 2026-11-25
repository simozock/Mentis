#include "hardwarekeyboard.h"

#include <QGuiApplication>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

namespace {
// Qt types into a focused, invisible org.qtproject.qt.android.QtEditText laid over the field.
// Once a hardware key takes Android out of touch mode, Android shades the focused view
// (View.setDefaultFocusHighlightEnabled, API 26): a light rectangle over the title that stayed on
// the topic after the edit (user's report 2026-11-16). Called on Android's main thread.
void disableFocusHighlight(const QJniObject &view) {
    if (!view.isValid()) return;
    const QString name = view.callObjectMethod("getClass", "()Ljava/lang/Class;").callObjectMethod<jstring>("getName").toString();
    if (name == QLatin1String("org.qtproject.qt.android.QtEditText")) {
        view.callMethod<void>("setDefaultFocusHighlightEnabled", "(Z)V", jboolean(false));
        return;
    }
    QJniEnvironment env;
    if (!env->IsInstanceOf(view.object(), env.findClass("android/view/ViewGroup"))) return;
    const jint count = view.callMethod<jint>("getChildCount");
    for (jint i = 0; i < count; ++i)
        disableFocusHighlight(view.callObjectMethod("getChildAt", "(I)Landroid/view/View;", i));
}
}  // namespace
#endif

HardwareKeyboard::HardwareKeyboard(QObject *parent) : QObject(parent) {
#ifdef Q_OS_ANDROID
    auto *app = qGuiApp;
    connect(app, &QGuiApplication::focusObjectChanged, this, &HardwareKeyboard::update);
    connect(app, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) update();
    });
    update();
#endif
}

void HardwareKeyboard::update() {
#ifdef Q_OS_ANDROID
    // Configuration.keyboard and hardKeyboardHidden
    // (https://developer.android.com/reference/android/content/res/Configuration#keyboard).
    constexpr jint kNoKeys = 1;             // KEYBOARD_NOKEYS
    constexpr jint kHardKeyboardShown = 1;  // HARDKEYBOARDHIDDEN_NO
    const QJniObject context(QNativeInterface::QAndroidApplication::context().object());
    const QJniObject resources = context.callObjectMethod("getResources", "()Landroid/content/res/Resources;");
    if (!resources.isValid()) return;
    const QJniObject configuration = resources.callObjectMethod("getConfiguration", "()Landroid/content/res/Configuration;");
    if (!configuration.isValid()) return;
    const bool attached = configuration.getField<jint>("keyboard") != kNoKeys
                          && configuration.getField<jint>("hardKeyboardHidden") == kHardKeyboardShown;
    // Every window has its own field, made with the window: looked for again on each focus change.
    (void)QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        const QJniObject activity(QNativeInterface::QAndroidApplication::context().object());
        const QJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
        if (window.isValid()) disableFocusHighlight(window.callObjectMethod("getDecorView", "()Landroid/view/View;"));
    });
    if (int(attached) == m_attached) return;
    m_attached = int(attached);
    // Set on Android's main thread, as the activity's window must be; queued there before the
    // show the focused field asks next.
    (void)QNativeInterface::QAndroidApplication::runOnAndroidMainThread([attached] {
        // https://developer.android.com/reference/android/view/WindowManager.LayoutParams#FLAG_ALT_FOCUSABLE_IM
        constexpr jint kAltFocusableIm = 0x00020000;
        const QJniObject activity(QNativeInterface::QAndroidApplication::context().object());
        const QJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
        if (!window.isValid()) return;
        if (attached) window.callMethod<void>("addFlags", "(I)V", kAltFocusableIm);
        else window.callMethod<void>("clearFlags", "(I)V", kAltFocusableIm);
    });
#endif
}
