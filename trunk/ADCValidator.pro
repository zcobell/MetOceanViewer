#-------------------------------------------------
#
# Project created by QtCreator 2013-11-19T21:34:38
#
#-------------------------------------------------

QT       += core gui webkitwidgets network xml

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = ADCValidator
TEMPLATE = app


SOURCES += main.cpp\
        ADCvalidator_main.cpp \
    ui_hwm_tab.cpp \
    ui_imeds_tab.cpp \
    ui_noaa_tab.cpp \
    imeds_data.cpp \
    hwm_data.cpp \
    imeds_functions.cpp \
    hwm_functions.cpp \
    noaa_functions.cpp \
    general_functions.cpp

HEADERS  += ADCvalidator.h \
    imeds.h \
    hwm.h

FORMS    += ADCvalidator_main.ui

OTHER_FILES +=

RESOURCES += \
    ADCvalidatior.qrc
