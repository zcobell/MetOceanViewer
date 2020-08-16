#include "userdatatab.h"

UserdataTab::UserdataTab(QWidget *parent)
    : QWidget(parent), m_type(TabType::USERTS) {
  this->initialize();
}

TabType UserdataTab::type() const { return this->m_type; }

void UserdataTab::initialize() {
  this->m_tabWidget = new QTabWidget(this);
  this->m_windowLayout = new QVBoxLayout(this);
  this->m_tabWidget->setSizePolicy(
      QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding));
  this->m_tabWidget->sizePolicy().setHorizontalStretch(1);
  this->m_windowLayout->addSpacing(0);
  this->m_windowLayout->setContentsMargins(0, 0, 0, 0);

  this->m_windowLayout->addWidget(this->m_tabWidget);

  this->m_mapChartWidget = new UserdataMap(this);
  this->m_dataTableWidget = new UserdataTable(this);

  this->m_mapChartWidget->initialize();

  this->m_tabWidget->addTab(this->m_dataTableWidget, "Data");
  this->m_tabWidget->addTab(this->m_mapChartWidget, "Map");
}
