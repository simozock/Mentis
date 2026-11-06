#pragma once

#include <QString>
#include <functional>

class QKeyEvent;
class QWidget;

// FEAT-092: the user manual, from anywhere. The main window registers how a page is opened; a
// dialog only names its page (setPage), and F1 or its "?" opens it. Pages are paths inside
// manual/, such as "files/export-pdf.md".
namespace Help {

// Who opens pages (the main window's manual window); empty: nothing happens.
void setOpener(std::function<void(const QString &page)> opener);
void open(const QString &page);

// The page of a window or a part of it: F1 in a dialog opens the nearest page up its parents.
void setPage(QWidget *widget, const QString &page);
QString pageOf(const QWidget *widget);

// F1 (or the key isHelpKey accepts) in a modal dialog opens the page of the dialog, or of the part
// that has the focus; the main window's own command does it elsewhere. One filter for the app: a
// later call replaces the key test (the callers guard their own lifetime).
void installDialogKey(std::function<bool(const QKeyEvent *)> isHelpKey);

// The main page of the manual.
inline constexpr const char *kIndexPage = "README.md";

}  // namespace Help
