#ifndef TABWIDGET_H
#define TABWIDGET_H

#include <QLabel>
#include <QTabWidget>
#include <QToolButton>

#include "stationlist.h"
#include "tabbar.h"

class TabWidget : public QTabWidget {
  Q_OBJECT
 public:
  TabWidget(QWidget *parent = nullptr);

 public slots:
  void addNewTab();

 private slots:
  void closeTab(int);

 private:
  TabBar *m_tabBar;
  QToolButton *m_addTabButton;
  StationList *m_stationList;
};

#endif  // TABWIDGET_H
