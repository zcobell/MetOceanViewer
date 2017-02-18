#include "movusertimeseriesoptions.h"
#include "ui_movusertimeseriesoptions.h"

movUserTimeseriesOptions::movUserTimeseriesOptions(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::movUserTimeseriesOptions)
{
    ui->setupUi(this);
    this->_mDisplayValues = false;
    this->_mHideInfoWindows = false;
}

movUserTimeseriesOptions::~movUserTimeseriesOptions()
{
    delete ui;
}

bool movUserTimeseriesOptions::displayValues()
{
    return this->_mDisplayValues;
}

bool movUserTimeseriesOptions::hideInfoWindows()
{
    return this->_mHideInfoWindows;
}

void movUserTimeseriesOptions::on_check_displayValues_toggled(bool checked)
{
    this->_mDisplayValues = checked;
    return;
}

void movUserTimeseriesOptions::on_check_hideBubbles_toggled(bool checked)
{
    this->_mHideInfoWindows = checked;
    return;
}

void movUserTimeseriesOptions::setDisplayValues(bool value)
{
    this->_mDisplayValues = value;
    return;
}

void movUserTimeseriesOptions::setHideInfoWindows(bool value)
{
    this->_mHideInfoWindows = value;
    return;
}

int movUserTimeseriesOptions::exec()
{
    ui->check_displayValues->setChecked(this->_mDisplayValues);
    ui->check_hideBubbles->setChecked(this->_mHideInfoWindows);
    return QDialog::exec();
}

void movUserTimeseriesOptions::setShowHideInfoWindowOption(bool value)
{
    if(value)
        ui->check_hideBubbles->show();
    else
        ui->check_hideBubbles->hide();
    return;
}
