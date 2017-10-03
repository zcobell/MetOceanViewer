#ifndef MOVQWEBENGINEVIEW_H
#define MOVQWEBENGINEVIEW_H

#include <QWebEngineView>
#include <QEvent>
#include <QKeyEvent>

class MovQWebEngineView : public QWebEngineView
{
    Q_OBJECT
public:
    MovQWebEngineView(QWidget *parent = 0): QWebEngineView(parent){}
signals:
    void enterKeyPressed();
protected:
    bool event(QEvent *e)
    {
        if (e->type()==QEvent::ShortcutOverride)
        {
            QKeyEvent* key = static_cast<QKeyEvent*>(e);
            if ( (key->key()==Qt::Key_Enter) || (key->key()==Qt::Key_Return) )
            {
                emit enterKeyPressed();
                return QObject::event(e);
            }
            else
                return QObject::event(e);
        }
        else
            return QObject::event(e);
    }
};

#endif // MOVQWEBENGINEVIEW_H
