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
    imeds.cpp \
    hwm.cpp

HEADERS  += ADCvalidator.h \
    imeds.h \
    hwm.h

FORMS    += ADCvalidator_main.ui

OTHER_FILES +=

RESOURCES += \
    ADCvalidatior.qrc
