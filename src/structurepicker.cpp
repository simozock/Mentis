#include "structurepicker.h"

#include "mapstructure.h"
#include "stylechoices.h"
#include "stylecontrols.h"
#include "uitheme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>

// FEAT-119: the types, their order and the names are StyleChoices', shared with the phone.
QString StructurePicker::typeName(MapStructure::Type type) { return StyleChoices::structureName(type); }

StructurePicker::StructurePicker(bool allowInherit, bool withLabel, QWidget *parent) : QWidget(parent), m_allowInherit(allowInherit) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    QList<std::pair<QString, QString>> items;
    if (allowInherit) items.append({QString(), tr("Same as parent")});
    for (const MapStructure::Type type : StyleChoices::structureTypes()) items.append({MapStructure::typeKey(type), typeName(type)});
    m_type = makeStructureSelect(items, this);
    m_type->setObjectName(QStringLiteral("StructureTypeSelect"));
    m_type->setAccessibleName(tr("Structure"));
    m_direction = makeDarkSelect(this);
    m_direction->setObjectName(QStringLiteral("StructureDirectionSelect"));
    m_direction->setAccessibleName(tr("Direction"));
    if (withLabel) {
        auto *row = new QHBoxLayout;
        row->setSpacing(8);
        auto *label = new QLabel(tr("Structure"), this);
        label->setStyleSheet(UiTheme::qss("color:@text; font-size:14px; background:transparent;"));
        row->addWidget(label);
        row->addStretch();
        row->addWidget(m_type);
        layout->addLayout(row);
    }
    layout->addWidget(m_direction);
    // La voce scelta arriva come indice: il menu a griglia (FEAT-038) non cambia la voce corrente
    // prima di activated (bug 2026-10-21: si leggeva la vecchia e non succedeva nulla).
    connect(m_type, &QComboBox::activated, this, [this](int index) {
        m_type->setCurrentIndex(index);
        const QString type = m_type->itemData(index).toString();
        const QString direction = type.isEmpty() ? QString()
            : MapStructure::directionKey(MapStructure::directions(MapStructure::typeFromKey(type)).front());
        fillDirections(type, direction);
        emit chosen(type, direction);
    });
    connect(m_direction, &QComboBox::activated, this, [this](int index) {
        emit chosen(m_type->currentData().toString(), m_direction->itemData(index).toString());
    });
}

void StructurePicker::setInTable(bool inTable) {
    const QString mindMap = MapStructure::typeKey(MapStructure::Type::MindMap);
    const int index = m_type->findData(mindMap);
    if (inTable && index >= 0) m_type->removeItem(index);
    else if (!inTable && index < 0) insertStructureItem(m_type, m_allowInherit ? 1 : 0, mindMap, typeName(MapStructure::Type::MindMap));
}

void StructurePicker::setValue(const QString &typeKey, const QString &directionKey) {
    const QSignalBlocker blocker(m_type);
    const QString type = typeKey.isEmpty() && !m_allowInherit ? MapStructure::typeKey(MapStructure::Type::MindMap) : typeKey;
    m_type->setCurrentIndex(std::max(0, m_type->findData(type)));
    fillDirections(type, directionKey);
}

void StructurePicker::fillDirections(const QString &typeKey, const QString &directionKey) {
    const QSignalBlocker blocker(m_direction);
    m_direction->clear();
    if (typeKey.isEmpty()) {  // ereditato: la direzione e' quella del genitore
        m_direction->setVisible(false);
        return;
    }
    const MapStructure::Type type = MapStructure::typeFromKey(typeKey);
    for (const MapStructure::Direction direction : MapStructure::directions(type))
        m_direction->addItem(StyleChoices::directionName(direction), MapStructure::directionKey(direction));
    const QString current = MapStructure::directionKey(MapStructure::directionFromKey(directionKey, type));
    m_direction->setCurrentIndex(std::max(0, m_direction->findData(current)));
    m_direction->setVisible(true);
    // Only the directions that change something can be chosen; the select is disabled when none of them
    // is another than the current one (user request 2026-11-24: choosing some did nothing).
    const QList<MapStructure::Direction> effective = m_effective ? m_effective(type) : MapStructure::directions(type);
    auto *items = qobject_cast<QStandardItemModel *>(m_direction->model());
    bool choice = false;
    for (int i = 0; i < m_direction->count(); ++i) {
        const QString key = m_direction->itemData(i).toString();
        const bool enabled = effective.contains(MapStructure::directionFromKey(key, type));
        if (items) items->item(i)->setEnabled(enabled);
        choice = choice || (enabled && key != current);
    }
    m_direction->setEnabled(choice);
}
