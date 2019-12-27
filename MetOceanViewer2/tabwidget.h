#ifndef TABWIDGET_H
#define TABWIDGET_H

#include <QPushButton>
#include <QTabWidget>
#include "stationlist.h"

class TabWidget : public QTabWidget {
  Q_OBJECT
 public:
  TabWidget(QWidget *parent = nullptr);

 private slots:
  void addNewTab();
  void closeTab(int);

 private:
  QPushButton *m_addTabButton;
  StationList *m_stationList;
};

#endif  // TABWIDGET_H
