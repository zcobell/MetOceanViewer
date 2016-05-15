#ifndef XTIDE_H
#define XTIDE_H

#include <QObject>
#include <QtWidgets>
#include <QWebEngineView>

#include "mov_qchartview.h"

class XTide : public QObject
{
    Q_OBJECT
public:
    explicit XTide(QWebEngineView *inMap, mov_QChartView *inChart,
                   QDateEdit *inStartDateEdit, QDateEdit *inEndDateEdit,
                   QComboBox *inUnits, QStatusBar *inStatusBar, QObject *parent = 0);

signals:

public slots:
};

#endif // XTIDE_H
