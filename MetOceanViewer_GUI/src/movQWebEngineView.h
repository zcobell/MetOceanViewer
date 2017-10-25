#ifndef MOVQWEBENGINEVIEW_H
#define MOVQWEBENGINEVIEW_H

#include <QEvent>
#include <QKeyEvent>
#include <QWebEngineView>

class MovQWebEngineView : public QWebEngineView {
  Q_OBJECT
public:
  MovQWebEngineView(QWidget *parent = 0) : QWebEngineView(parent) {}

//...This accounts for a bug in Qt v5.9.1
//   If the user cursor is not in the map, the
//   enter key "plot" signal is not sent. In the
//   case of Qt5.9.1, we make this connection manually
//   but otherwise there is no need.
#if (QT_VERSION == QT_VERSION_CHECK(5, 9, 1))
signals:

  void enterKeyPressed();

protected:
  bool event(QEvent *e) {
    if (e->type() == QEvent::ShortcutOverride) {
      QKeyEvent *key = static_cast<QKeyEvent *>(e);
      if ((key->key() == Qt::Key_Enter) || (key->key() == Qt::Key_Return)) {
        emit enterKeyPressed();
        return QObject::event(e);
      } else
        return QObject::event(e);
    } else
      return QObject::event(e);
  }
#endif
};

#endif // MOVQWEBENGINEVIEW_H
