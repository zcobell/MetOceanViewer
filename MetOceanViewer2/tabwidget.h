#ifndef TABWIDGET_H
#define TABWIDGET_H

#include <QLabel>
#include <QTabWidget>
#include <QToolButton>

#include "mapfunctions.h"
#include "newtabdialog.h"
#include "stationlist.h"
#include "tabbar.h"

class TabWidget : public QTabWidget {
  Q_OBJECT

 public:
  TabWidget(QWidget *parent = nullptr);

 public slots:
  void addNewTab();
  void changeBasemapEsri();
  void changeBasemapOsm();
  void changeBasemapMapbox();

 private slots:
  void closeTab(int);

 signals:
  void signalChangeBasemapEsri();
  void signalChangeBasemapOsm();
  void signalChangeBasemapMapbox();

 private:
  void addNoaaTab(const NewTabDialog *d, std::vector<MovStation> *stations);
  void addUsgsTab(const NewTabDialog *d, std::vector<MovStation> *stations);
  void addXtideTab(const NewTabDialog *d, std::vector<MovStation> *stations);
  void addNdbcTab(const NewTabDialog *d, std::vector<MovStation> *stations);
  void addUserdataTab(const NewTabDialog *d);

  TabBar *m_tabBar;
  QToolButton *m_addTabButton;
  StationList *m_stationList;
  void addNewTabButton();
};

#endif  // TABWIDGET_H
