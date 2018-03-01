#ifndef MOV_FILETYPES_H
#define MOV_FILETYPES_H

#include <QObject>
#include "metoceanviewer.h"

class movFiletypes : public QObject {
  Q_OBJECT
public:
  explicit movFiletypes(QObject *parent = nullptr);

  static int getIntegerFiletype(QString filename);
  static QString getStringFiletype(QString filename);
  static QString integerFiletypeToString(int filetype);

private:
  static bool _checkNetcdfAdcirc(QString filename);
  static bool _checkNetcdfDflow(QString filename);
  static bool _checkASCIIAdcirc(QString filename);
  static bool _checkASCIIImeds(QString filename);
  static bool _checkNetcdfGeneric(QString filename);
};

#endif // MOV_FILETYPES_H
