#ifndef CHARTOPTIONSMENU_H
#define CHARTOPTIONSMENU_H

#include <QMenu>
#include <QPushButton>

class ChartOptionsMenu : public QPushButton {
  Q_OBJECT
 public:
  explicit ChartOptionsMenu(QWidget *parent = nullptr);

 signals:
  void displayValuesTriggered(bool);
  void saveDataTriggered();
  void fitMarkersTriggered();
  void resetChartTriggered();
  void saveGraphicTriggered();

 private slots:
  void emitSaveDataSignal();
  void emitSaveGraphicSignal();
  void emitFitMarkersSignal();
  void emitResetChartSignal();
  void emitDisplayValuesSignal(bool);

 private:
  QMenu *m_menu;
  QAction *m_displayValues;
  QAction *m_saveData;
  QAction *m_saveGraphic;
  QAction *m_fitMarkers;
  QAction *m_resetChart;
};

#endif  // CHARTOPTIONSMENU_H
