#pragma once

#include <QString>

// FEAT-114 / FEAT-120 stage C: a file of the app's files folder (Android shares only what Qt's
// FileProvider covers) handed to Android's share sheet; elsewhere its folder is shown.
namespace MobileShare {
bool share(const QString &file, const QString &chooserTitle);
}

#ifdef Q_OS_ANDROID
// ACTION_VIEW (open with) or ACTION_SEND (share) through Qt's FileProvider, in the system's chooser.
bool startAndroidFileIntent(const QString &file, bool share, const QString &chooserTitle);
#endif
