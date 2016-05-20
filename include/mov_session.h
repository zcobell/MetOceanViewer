#ifndef MOV_SESSION_H
#define MOV_SESSION_H

#include <QObject>
#include <netcdf.h>
#include <QtWidgets>
#include <QFileDialog>
#include "mov_generic.h"

class mov_session : public QObject
{

    Q_OBJECT

public:
    explicit mov_session(QTableWidget *inTableWidget,
                         QLineEdit *inPlotTitleWidget,
                         QLineEdit *inXLabelWidget,
                         QLineEdit *inYLabelWidget,
                         QDateEdit *inStartDateEdit,
                         QDateEdit *inEndDateEdit,
                         QDoubleSpinBox *inYMinSpinBox,
                         QDoubleSpinBox *inYMaxSpinBox,
                         QCheckBox *incheckAllData,
                         QCheckBox *inCheckYAuto,
                         QString &inPreviousDirectory,
                         QObject *parent = 0);

    int save();
    int open(QString openFilename);
    int setSessionFilename(QString filename);
    QString getSessionFilename();

signals:
    void sessionError(QString);

private:
    QTableWidget   *tableWidget;
    QLineEdit      *plotTitleWidget;
    QLineEdit      *xLabelWidget;
    QLineEdit      *yLabelWidget;
    QDateEdit      *startDateEdit;
    QDateEdit      *endDateEdit;
    QDoubleSpinBox *yMinSpinBox;
    QDoubleSpinBox *yMaxSpinBox;
    QCheckBox      *checkAllData;
    QCheckBox      *checkYAuto;
    QString         previousDirectory;
    QString         currentDirectory;
    QString         sessionFileName;
    QString         alternateFolder;

};

#endif // MOV_SESSION_H
