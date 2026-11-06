#pragma once

// The mouse wheel over a closed select does not change its value (user's request 2026-10-26): the
// event goes on to the parent, so the panel or the map under the select scrolls instead. With the list
// open the wheel scrolls the list as usual. Covers every QComboBox of the application.

#include <QObject>

class WheelGuard final : public QObject {
public:
    static void install();  // once, after QApplication exists

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    using QObject::QObject;
    QObject *m_comboFilter = nullptr;
};
