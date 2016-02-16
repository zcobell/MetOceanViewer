#ifndef KEYHANDLER_H
#define KEYHANDLER_H

#include <QObject>

class keyhandler : public QObject
{
    Q_OBJECT
protected:
    bool eventFilter(QObject* obj, QEvent* event);

signals:
    void enterKeyPressed();
};

#endif // KEYHANDLER_H
