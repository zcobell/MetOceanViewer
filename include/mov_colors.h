#ifndef MOV_COLORS_H
#define MOV_COLORS_H

#include <QObject>
#include <QColor>
#include <QTime>

class mov_colors : public QObject
{

public:
    explicit mov_colors(QObject *parent = 0);

    static QString MakeColorString(QColor InputColor);

    static QColor GenerateRandomColor();

    static QColor styleSheetToColor(QString stylesheet);

};

#endif // MOV_COLORS_H
