#include "javascriptAsyncReturn.h"

javascriptAsyncReturn::javascriptAsyncReturn(QObject *parent) : QObject(parent)
{

}

void javascriptAsyncReturn::setValue(QVariant value)
{
    this->javaVariable = value.toString();
    emit valueChanged(this->javaVariable);
}

QString javascriptAsyncReturn::getValue()
{
    QString returnValue = this->javaVariable;
    return returnValue;
}
