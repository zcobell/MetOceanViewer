#ifndef FILEINFO_H
#define FILEINFO_H

#include <QString>

namespace FileInfo {
  QString directory(const QString &filepath,
                           bool changePreviousDir = false);
  void changeLastDirectory(const QString &path);
  QString extension(const QString &filepath);
  QString basename(const QString &filepath);
  bool exists(const QString &filepath);
  QString lastDirectory();
};

#endif  // FILEINFO_H
