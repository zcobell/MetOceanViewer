#ifndef TABBAR_H
#define TABBAR_H

#include <QTabBar>

class TabBar : public QTabBar {
  Q_OBJECT
 public:
  TabBar(QWidget *parent = nullptr);

 public slots:
  void mouseDoubleClickEvent(QMouseEvent *e) override;

};

#endif  // TABBAR_H
