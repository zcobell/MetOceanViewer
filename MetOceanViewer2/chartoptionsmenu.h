#ifndef CHARTOPTIONSMENU_H
#define CHARTOPTIONSMENU_H

#include <QMenu>
#include <QPushButton>

class ChartOptionsMenu : public QPushButton {
  Q_OBJECT
 public:
  explicit ChartOptionsMenu(bool displayValues, bool saveData, bool saveGraphic,
                            bool fitMarkers, bool resetChart, bool options,
                            QWidget *parent = nullptr);

  int titleFontsize() const;

  int axisFontsize() const;

  int legendFontsize() const;

  QString dateFormat() const;

 signals:
  void displayValuesTriggered(bool);
  void saveDataTriggered();
  void fitMarkersTriggered();
  void resetChartTriggered();
  void saveGraphicTriggered();
  void chartOptionsTriggered();
  void chartOptionsChanged();

 private slots:
  void emitSaveDataSignal();
  void emitSaveGraphicSignal();
  void emitFitMarkersSignal();
  void emitResetChartSignal();
  void emitDisplayValuesSignal(bool);
  void showChartOptions();
  void emitChartOptionsChanged();

 private:
  QMenu *m_menu;
  QAction *m_displayValues;
  QAction *m_saveData;
  QAction *m_saveGraphic;
  QAction *m_fitMarkers;
  QAction *m_resetChart;
  QAction *m_options;

  int m_titleFontsize;
  int m_axisFontsize;
  int m_legendFontsize;
  QString m_dateFormat;
};

#endif  // CHARTOPTIONSMENU_H
