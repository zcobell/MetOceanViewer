#ifndef TABBAR_H
#define TABBAR_H

#include <QTabBar>

class TabBar : public QTabBar
{
    Q_OBJECT
public:
    TabBar(QWidget *parent = nullptr);

    void mouseDoubleClickEvent(QMouseEvent *e);
};

#endif // TABBAR_H
