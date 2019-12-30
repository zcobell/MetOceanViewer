#include "tabwidget.h"

#include <QVariant>

#include "newtabdialog.h"
#include "noaatab.h"
#include "tabbar.h"

TabWidget::TabWidget(QWidget* parent) : QTabWidget(parent) {
  //...Override the tab bar so that a double click renames
  this->m_tabBar = new TabBar(this);
  this->m_tabBar->setObjectName("TabBar");
  this->setTabBar(this->m_tabBar);
  this->setObjectName("TabWidget");
  this->setStyleSheet(
      "#qt_tabwidget_stackedwidget{"
      "background-image: url(:/rsc/img/mov.png);"
      "background-repeat: no-repeat; "
      "background-attachment: fixed; "
      "background-position: center; "
      "}");

  //...Add a button to add new tabs
  this->m_addTabButton = new QToolButton(this);
  this->m_addTabButton->setObjectName("addButton");
  this->m_addTabButton->setAutoFillBackground(false);
  this->m_addTabButton->setText("+");
  this->m_addTabButton->setToolButtonStyle(
      Qt::ToolButtonStyle::ToolButtonTextOnly);
  this->setCornerWidget(this->m_addTabButton, Qt::TopRightCorner);
  this->m_addTabButton->setMinimumSize(this->m_addTabButton->sizeHint());
  connect(this->m_addTabButton, SIGNAL(clicked()), this, SLOT(addNewTab()));

  //...Allow tabs to be closed and connect signals/slots
  this->setTabsClosable(true);
  connect(this, SIGNAL(tabCloseRequested(int)), this, SLOT(closeTab(int)));

  //...Generate the station lists by reading the data
  // from disk once
  this->m_stationList = new StationList();
}

void TabWidget::addNewTab() {
  //...Pop up a dialog to generate a new tab
  NewTabDialog* d = new NewTabDialog(this);

  //...If the user doesn't close the tab, get the station list and
  // send to the newly created tab
  if (d->exec() == QDialog::Accepted) {
    QVector<Station>* stations = this->m_stationList->get(d->type());
    switch (d->type()) {
      case TabType::NOAA:
        this->addTab(new NoaaTab(stations, this), d->tabName());
        this->setCurrentIndex(this->count() - 1);
        break;
      default:
        break;
    }
  }

  //...Delete the dialog
  d->deleteLater();
}

void TabWidget::closeTab(int index) {
  //...Remove the tab
  this->removeTab(index);
  return;
}

// QVariant property = this->widget(index)->property("workerObject");
// NoaaTab *ptr = (NoaaTab*) property.value<intptr_t>();
