#include "structurepicker.h"

#include "mapstructure.h"
#include "stylecontrols.h"
#include "uitheme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {
// Tipi delle fasi C ed E (parentesi graffa, linea del tempo; gli altri seguiranno), nell'ordine della galleria.
struct TypeEntry { MapStructure::Type type; };
const QList<TypeEntry> &entries() {
    static const QList<TypeEntry> list{
        {MapStructure::Type::MindMap}, {MapStructure::Type::LogicChart}, {MapStructure::Type::Brace}, {MapStructure::Type::OrgChart},
        {MapStructure::Type::TreeChart}, {MapStructure::Type::Timeline}, {MapStructure::Type::Fishbone},
        {MapStructure::Type::TreeTable},  // FEAT-028
    };
    return list;
}

}  // namespace

QString StructurePicker::typeName(MapStructure::Type type) {
    switch (type) {
    case MapStructure::Type::LogicChart: return StructurePicker::tr("Logic chart");
    case MapStructure::Type::Brace: return StructurePicker::tr("Brace map");
    case MapStructure::Type::OrgChart: return StructurePicker::tr("Org chart");
    case MapStructure::Type::TreeChart: return StructurePicker::tr("Tree chart");
    case MapStructure::Type::Timeline: return StructurePicker::tr("Timeline");
    case MapStructure::Type::Fishbone: return StructurePicker::tr("Fishbone");
    case MapStructure::Type::TreeTable: return StructurePicker::tr("Tree table");
    default: return StructurePicker::tr("Mind map");
    }
}

namespace {

QString directionName(MapStructure::Direction direction) {
    switch (direction) {
    case MapStructure::Direction::Right: return StructurePicker::tr("Right");
    case MapStructure::Direction::Left: return StructurePicker::tr("Left");
    case MapStructure::Direction::Down: return StructurePicker::tr("Down");
    case MapStructure::Direction::Up: return StructurePicker::tr("Up");
    case MapStructure::Direction::OffAxis: return StructurePicker::tr("Off axis");
    default: return StructurePicker::tr("Both sides");
    }
}
}  // namespace

StructurePicker::StructurePicker(bool allowInherit, bool withLabel, QWidget *parent) : QWidget(parent), m_allowInherit(allowInherit) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    QList<std::pair<QString, QString>> items;
    if (allowInherit) items.append({QString(), tr("Same as parent")});
    for (const TypeEntry &entry : entries()) items.append({MapStructure::typeKey(entry.type), typeName(entry.type)});
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
        m_direction->addItem(directionName(direction), MapStructure::directionKey(direction));
    const QString current = MapStructure::directionKey(MapStructure::directionFromKey(directionKey, type));
    m_direction->setCurrentIndex(std::max(0, m_direction->findData(current)));
    m_direction->setVisible(true);
    m_direction->setEnabled(m_direction->count() > 1);
}
