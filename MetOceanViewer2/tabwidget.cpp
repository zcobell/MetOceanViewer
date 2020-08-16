#include "tabwidget.h"

#include <QVariant>

#include "ndbctab.h"
#include "newtabdialog.h"
#include "noaatab.h"
#include "tabbar.h"
#include "userdatatab.h"
#include "usgstab.h"
#include "xtidetab.h"

TabWidget::TabWidget(QWidget* parent) : QTabWidget(parent) {
  //...Override the tab bar so that a double click renames
  this->m_tabBar = new TabBar(this);
  this->m_tabBar->setObjectName("TabBar");
  this->setTabBar(this->m_tabBar);
  this->setObjectName("MainTabs");
  this->setAccessibleName("MainTabs");
  this->setStyleSheet(
      "#MainTabs::pane {"
      "background-image: url(:/rsc/img/mov.png);"
      "background-repeat: no-repeat; "
      "background-attachment: fixed; "
      "background-position: center; "
      "}");

  //...Add a button to add new tabs
  // this->addNewTabButton();

  //...Allow tabs to be closed and connect signals/slots
  this->setTabsClosable(true);
  connect(this, SIGNAL(tabCloseRequested(int)), this, SLOT(closeTab(int)));

  //...Generate the station lists by reading the data
  // from disk once
  this->m_stationList = new StationList();
}

void TabWidget::addNoaaTab(const NewTabDialog* d, QVector<Station>* stations) {
  NoaaTab* n = new NoaaTab(stations, this);
  connect(this, SIGNAL(signalChangeBasemapEsri()), n,
          SLOT(changeBasemapEsri()));
  connect(this, SIGNAL(signalChangeBasemapOsm()), n, SLOT(changeBasemapOsm()));
  connect(this, SIGNAL(signalChangeBasemapMapbox()), n,
          SLOT(changeBasemapMapbox()));
  this->addTab(n, d->tabName());
  this->setCurrentIndex(this->count() - 1);
}

void TabWidget::addUsgsTab(const NewTabDialog* d, QVector<Station>* stations) {
  UsgsTab* n = new UsgsTab(stations, this);
  connect(this, SIGNAL(signalChangeBasemapEsri()), n,
          SLOT(changeBasemapEsri()));
  connect(this, SIGNAL(signalChangeBasemapOsm()), n, SLOT(changeBasemapOsm()));
  connect(this, SIGNAL(signalChangeBasemapMapbox()), n,
          SLOT(changeBasemapMapbox()));
  this->addTab(n, d->tabName());
  this->setCurrentIndex(this->count() - 1);
}

void TabWidget::addXtideTab(const NewTabDialog* d, QVector<Station>* stations) {
  XTideTab* n = new XTideTab(stations, this);
  connect(this, SIGNAL(signalChangeBasemapEsri()), n,
          SLOT(changeBasemapEsri()));
  connect(this, SIGNAL(signalChangeBasemapOsm()), n, SLOT(changeBasemapOsm()));
  connect(this, SIGNAL(signalChangeBasemapMapbox()), n,
          SLOT(changeBasemapMapbox()));
  this->addTab(n, d->tabName());
  this->setCurrentIndex(this->count() - 1);
}

void TabWidget::addNdbcTab(const NewTabDialog* d, QVector<Station>* stations) {
  NdbcTab* n = new NdbcTab(stations, this);
  connect(this, SIGNAL(signalChangeBasemapEsri()), n,
          SLOT(changeBasemapEsri()));
  connect(this, SIGNAL(signalChangeBasemapOsm()), n, SLOT(changeBasemapOsm()));
  connect(this, SIGNAL(signalChangeBasemapMapbox()), n,
          SLOT(changeBasemapMapbox()));
  this->addTab(n, d->tabName());
  this->setCurrentIndex(this->count() - 1);
}

void TabWidget::addUserdataTab(const NewTabDialog* d) {
  UserdataTab* n = new UserdataTab(this);
  this->addTab(n, d->tabName());
  this->setCurrentIndex(this->count() - 1);
}

void TabWidget::addNewTab() {
  //...Pop up a dialog to generate a new tab
  NewTabDialog* d = new NewTabDialog(this);

  //...If the user doesn't close the tab, get the station list and
  // send to the newly created tab
  if (d->exec() == QDialog::Accepted) {
    switch (d->type()) {
      case TabType::NOAA:
        this->addNoaaTab(d, this->m_stationList->get(d->type()));
        break;
      case TabType::USGS:
        this->addUsgsTab(d, this->m_stationList->get(d->type()));
        break;
      case TabType::XTIDE:
        this->addXtideTab(d, this->m_stationList->get(d->type()));
        break;
      case TabType::NDBC:
        this->addNdbcTab(d, this->m_stationList->get(d->type()));
        break;
      case TabType::USERTS:
        this->addUserdataTab(d);
      default:
        break;
    }
  }

  //...Delete the dialog
  d->deleteLater();
}

void TabWidget::changeBasemapEsri() { emit signalChangeBasemapEsri(); }

void TabWidget::changeBasemapOsm() { emit signalChangeBasemapOsm(); }

void TabWidget::changeBasemapMapbox() { emit signalChangeBasemapMapbox(); }

void TabWidget::closeTab(int index) {
  //...Remove the tab
  this->removeTab(index);
  return;
}

void TabWidget::addNewTabButton() {
  this->m_addTabButton = new QToolButton(this);
  this->m_addTabButton->setObjectName("addButton");
  this->m_addTabButton->setAutoFillBackground(false);
  this->m_addTabButton->setText("+");
  this->m_addTabButton->setToolButtonStyle(
      Qt::ToolButtonStyle::ToolButtonTextOnly);
  this->setCornerWidget(this->m_addTabButton, Qt::TopRightCorner);
  this->m_addTabButton->setMinimumSize(this->m_addTabButton->sizeHint());
  connect(this->m_addTabButton, SIGNAL(clicked()), this, SLOT(addNewTab()));
}

// QVariant property = this->widget(index)->property("workerObject");
// NoaaTab *ptr = (NoaaTab*) property.value<intptr_t>();
