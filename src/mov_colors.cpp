#include "mov_colors.h"

mov_colors::mov_colors(QObject *parent) : QObject(parent)
{

}

//-------------------------------------------//
//Makes a string that sets the CSS style
//according to the input color
//-------------------------------------------//
QString mov_colors::MakeColorString(QColor InputColor)
{
    QString S("background-color: #"
                + QString(InputColor.red() < 16? "0" : "") + QString::number(InputColor.red(),16)
                + QString(InputColor.green() < 16? "0" : "") + QString::number(InputColor.green(),16)
                + QString(InputColor.blue() < 16? "0" : "") + QString::number(InputColor.blue(),16) + ";");
    return S;
}
//-------------------------------------------//


QColor mov_colors::styleSheetToColor(QString stylesheet)
{
    QColor thisColor;
    QString colorString;

    colorString = stylesheet.split(": ").value(1);
    colorString = colorString.split(";").value(0);
    thisColor.setNamedColor(colorString.simplified());

    return thisColor;
}


//-------------------------------------------//
//Generates a random color and optionally
//mixes in white to make it a more pastel
//type color. This is turned off by default
//-------------------------------------------//
QColor mov_colors::GenerateRandomColor()
{
    QColor MyColor, Mix;
    QTime SeedTime;
    bool DoMix;

    DoMix = false;

    SeedTime = QTime::currentTime();
    qsrand((uint)SeedTime.msec());

    if(DoMix)
    {
        Mix.setRed(255);
        Mix.setGreen(255);
        Mix.setBlue(255);

        MyColor.setRed((qrand()%255 + Mix.red()) / 2);
        MyColor.setGreen((qrand()%255 + Mix.green()) / 2);
        MyColor.setBlue((qrand()%255 + Mix.blue()) / 2);
    }
    else
    {
        MyColor.setRed(qrand()%255);
        MyColor.setGreen(qrand()%255);
        MyColor.setBlue(qrand()%255);
    }

    return MyColor;
}
//-------------------------------------------//
