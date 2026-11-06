#include "panelcontext.h"

PanelContext::Page PanelContext::selectionChanged(const QStringList &nodeIds) {
    if (nodeIds == m_selection) return m_current;
    m_selection = nodeIds;
    m_current = contextualPage(m_selection);
    return m_current;
}

PanelContext::Page PanelContext::manualChoice(Page page) {
    m_current = page;
    return m_current;
}

PanelContext::Page PanelContext::reopened(const QStringList &nodeIds) {
    m_selection = nodeIds;
    m_current = contextualPage(m_selection);
    return m_current;
}
