#ifndef NDBCTAB_H
#define NDBCTAB_H

#include <QObject>
#include "mapchartwidget.h"
#include <QPushButton>

class NdbcTab : public MapChartWidget
{
    Q_OBJECT
public:
    NdbcTab(QVector<Station> *stations, QWidget *parent = nullptr);
    void plot() override;

private slots:

private:
    QGroupBox * generateInputBox() override;
    void connectSignals() override;

    int getDataFromNdbc(const Station &s);

    QPushButton *m_btn_fetchData;

};

#endif // NDBCTAB_H
