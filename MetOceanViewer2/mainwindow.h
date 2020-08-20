#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "mapchartwidget.h"
#include "mapfunctions.h"
#include "tabwidget.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

 protected slots:
  void generateMenus();

 private slots:
  void setMapboxApiKey();
  void saveSettings();
  void setBasemapEsri();
  void setBasemapOsm();
  void setBasemapMapbox();

 private:
  Ui::MainWindow *ui;
  TabWidget *m_tabWidget;

  MapFunctions m_mapFunctions;

  QMenu *m_menuFile;
  QMenu *m_menuOptions;
  QMenu *m_menuMaptypes;
  QAction *m_actionNewTab;
  QAction *m_actionSelectMapEsri;
  QAction *m_actionSelectMapOsm;
  QAction *m_actionSelectMapMapbox;
  QAction *m_actionSetMapboxKey;
  QAction *m_actionSaveSettings;
};
#endif  // MAINWINDOW_H
