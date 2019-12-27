#ifndef NOAATAB_H
#define NOAATAB_H

#include <QObject>
#include "mapchartwidget.h"

class NoaaTab : public MapChartWidget
{
    Q_OBJECT
public:
    NoaaTab(QVector<Station> *stations, QWidget *parent = nullptr);

    void plot() override;

private:

    QGroupBox * generateInputBox() override;

    //...Widgets
    QDateTimeEdit *m_dte_startDate;
    QDateTimeEdit *m_dte_endDate;
    QPushButton *m_btn_refresh;
    QPushButton *m_btn_plot;
    QLabel *m_lbl_startDate;
    QLabel *m_lbl_endDate;
    QLabel *m_lbl_timezone;
    QLabel *m_lbl_datum;
    QLabel *m_lbl_datatype;
    QComboBox *m_cbx_timezones;
    QComboBox *m_cbx_datum;
    QComboBox *m_cbx_datatype;
    QCheckBox *m_chk_vdatum;
};

#endif // NOAATAB_H
