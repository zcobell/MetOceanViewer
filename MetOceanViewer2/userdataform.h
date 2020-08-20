#ifndef USERDATAFORM_H
#define USERDATAFORM_H

#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QString>
#include <memory>

#include "combobox.h"
#include "datebox.h"
#include "ezproj.h"
#include "textentry.h"
#include "unitsmenu.h"
#include "userdataseries.h"

class UserdataForm : public QDialog {
  Q_OBJECT
 public:
  UserdataForm(QWidget *parent = nullptr);

  void setSeriesData(const UserdataSeries &data);
  UserdataSeries series();

 private slots:
  void selectColor();
  void checkEpsgCode(int epsg);
  void showEpsgDescription();
  void browseFile();

 private:
  UserdataSeries m_series;

  TextEntry<QString> *m_filename;
  TextEntry<QString> *m_label;
  TextEntry<double> *m_unitConversion;
  TextEntry<double> *m_timeAdjustment;
  TextEntry<double> *m_yAdjustment;
  TextEntry<int> *m_coordinateSystem;
  TextEntry<QString> *m_fileType;
  QPushButton *m_colorSelectButton;
  QPushButton *m_colorShortcut1;
  QPushButton *m_colorShortcut2;
  QPushButton *m_colorShortcut3;
  QPushButton *m_colorShortcut4;
  QLabel *m_colorLabel;
  QPushButton *m_coordinateSystemDescription;
  QComboBox *m_timeUnits;
  UnitsMenu *m_unitMenu;
  ComboBox *m_linestyle;
  QPushButton *m_browseFile;
  QPushButton *m_unitMenuButton;
  QDialogButtonBox *m_buttons;
  std::unique_ptr<Ezproj> m_proj;
  QString m_defaultBoxStylesheet;
  QColor m_selectedColor;

  QHBoxLayout *makeFileLayout();
  QHBoxLayout *makeUnitLayout();
  QHBoxLayout *makeTimeLayout();
  QHBoxLayout *makeYshiftLayout();
  QHBoxLayout *makeColorLayout();
  QHBoxLayout *makeCoordinateLayout();
  QHBoxLayout *makeLinestyleLayout();
  void changeButtonColor(const QColor c);
  void getFormData();
  static double timeUnitConversion(const int index);
  static int timeUnitToIndex(const double t);
};

#endif  // USERDATAFORM_H
