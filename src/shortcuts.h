#pragma once

// Registro delle scorciatoie modificabili (FEAT-041, docs/features/FEAT-041-scorciatoie-modificabili.md).
// Comandi divisi in aree (Generale, Mappa, Outliner): uno stesso tasto puo' servire in aree diverse,
// mentre un comando Generale vale ovunque. Le scelte si salvano nelle impostazioni dell'app
// (shortcuts/<id>, forma portabile; chiave assente = predefinite). Nessuna dipendenza da widget.

#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QKeyEvent;

class ShortcutRegistry final : public QObject {
    Q_OBJECT
public:
    enum class Area { General, Map, Outliner, Gantt };
    static constexpr int kMaxShortcuts = 2;

    struct Command {
        QString id;
        Area area = Area::General;
        QString name;  // gia' tradotto
        QList<QKeySequence> defaults;
    };

    explicit ShortcutRegistry(QObject *parent = nullptr);

    // Registra un comando (fino a kMaxShortcuts predefinite). Un id gia' registrato si sostituisce.
    void add(const QString &id, Area area, const QString &name, const QList<QKeySequence> &defaults);
    QStringList ids() const;  // nell'ordine di registrazione
    QList<Command> commands(Area area) const;
    bool contains(const QString &id) const;
    Command command(const QString &id) const;

    // Scorciatoie attuali: scelte (anche nessuna) o predefinite.
    QList<QKeySequence> shortcuts(const QString &id) const;
    bool isDefault(const QString &id) const;
    // Al massimo kMaxShortcuts, senza vuote ne' doppioni; salvate subito.
    void setShortcuts(const QString &id, const QList<QKeySequence> &shortcuts);
    void reset(const QString &id);
    void resetAll();
    // Comando che usa gia' il tasto nella stessa area o fra i comandi Generali (o, per un comando
    // Generale, in qualunque area); vuoto se libero. Il comando id stesso non conta.
    QString conflict(const QString &id, const QKeySequence &shortcut) const;
    // Vero se l'evento corrisponde a una delle scorciatoie del comando (tasti gestiti dall'interfaccia).
    bool matches(const QString &id, const QKeyEvent *event) const;

    static QString areaName(Area area);

signals:
    void changed(const QString &id);

private:
    int indexOf(const QString &id) const;
    static QList<QKeySequence> cleaned(const QList<QKeySequence> &shortcuts);
    QList<Command> m_commands;
};
