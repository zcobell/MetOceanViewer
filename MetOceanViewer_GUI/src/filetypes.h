#ifndef FILETYPES_H
#define FILETYPES_H

#include <QObject>
#include "metoceanviewer.h"

class Filetypes : public QObject {
  Q_OBJECT
public:
  explicit Filetypes(QObject *parent = nullptr);

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

#endif // FILETYPES_H
