#ifndef CHARTOPTIONS_H
#define CHARTOPTIONS_H

#include <QDialog>

namespace Ui {
class ChartOptions;
}

class ChartOptions : public QDialog {
  Q_OBJECT

 public:
  explicit ChartOptions(int titleFontsize, int axisFontsize, int legendFontsize,
                        QString dateFormat, QWidget *parent = nullptr);
  ~ChartOptions();

  int titleFontsize() const;
  void setTitleFontsize(int titleFontsize);

  int axisFontsize() const;
  void setAxisFontsize(int axisFontsize);

  int legendFontsize() const;
  void setLegendFontsize(int legendFontsize);

  QString dateFormat() const;
  void setDateFormat(const QString &dateFormat);

 private:
  Ui::ChartOptions *ui;
};

#endif  // CHARTOPTIONS_H
