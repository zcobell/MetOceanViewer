#ifndef JAVASCRIPTASYNCRETURN_H
#define JAVASCRIPTASYNCRETURN_H

#include <QObject>
#include <QVariant>

class javascriptAsyncReturn : public QObject
{
    Q_OBJECT
public:
    explicit javascriptAsyncReturn(QObject *parent = 0);
    void setValue(QVariant value);
    QString getValue();

signals:
    void valueChanged(QString);

private:
    QString javaVariable;
};

#endif // JAVASCRIPTASYNCRETURN_H
