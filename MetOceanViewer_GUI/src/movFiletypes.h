#ifndef MOV_FILETYPES_H
#define MOV_FILETYPES_H

#include <QObject>

#define FILETYPE_NETCDF_ADCIRC  -99990
#define FILETYPE_NETCDF_DFLOW   -99991
#define FILETYPE_ASCII_ADCIRC   -99992
#define FILETYPE_ASCII_IMEDS    -99993
#define FILETYPE_ERROR          -88888

class movFiletypes : public QObject
{
    Q_OBJECT
public:
    explicit movFiletypes(QObject *parent = 0);

    static int getIntegerFiletype(QString filename);
    static QString getStringFiletype(QString filename);
    static QString integerFiletypeToString(int filetype);

private:
    static bool _checkNetcdfAdcirc(QString filename);
    static bool _checkNetcdfDflow(QString filename);
    static bool _checkASCIIAdcirc(QString filename);
    static bool _checkASCIIImeds(QString filename);

};

#endif // MOV_FILETYPES_H
