#include "tabbar.h"

#include <QInputDialog>
#include <QMouseEvent>
#include <QDebug>

TabBar::TabBar(QWidget *parent) : QTabBar(parent){}

void TabBar::mouseDoubleClickEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton) {
    QTabBar::mouseDoubleClickEvent(e);
    return;
  }

  int idx = this->currentIndex();
  bool ok = true;
  QString newName = QInputDialog::getText(this, "Change Tab Name",
                                          "Rename tab to: ", QLineEdit::Normal,
                                          tabText(idx), &ok);

  if (ok) {
    this->setTabText(idx, newName);
  }
}
