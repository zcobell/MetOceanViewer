#include "keyhandler.h"
#include <QKeyEvent>
#include <QDebug>

bool keyhandler::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type()==QEvent::KeyPress) {
        QKeyEvent* key = static_cast<QKeyEvent*>(event);
        if ( (key->key()==Qt::Key_Enter) || (key->key()==Qt::Key_Return) )
        {
            emit enterKeyPressed();
        }
        else
        {
            return QObject::eventFilter(obj, event);
        }
        return true;
    }
    else
    {
        return QObject::eventFilter(obj, event);
    }
    return false;
}
