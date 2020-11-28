#include "fileinfo.h"

#include <QDir>

static QString s_previousDirectory = QDir().homePath();

QString FileInfo::basename(const QString &filepath) {
  QFileInfo f(filepath);
  return f.completeBaseName();
}

QString FileInfo::extension(const QString &path) {
  QFileInfo f(path);
  return f.completeSuffix();
}

QString FileInfo::directory(const QString &filepath, bool changePreviousDir) {
  QFileInfo f(filepath);
  QString dir = f.dir().path();
  if (changePreviousDir) {
    s_previousDirectory = dir;
  }
  return dir;
}

void FileInfo::changeLastDirectory(const QString &path) {
  s_previousDirectory = FileInfo::directory(path);
}

bool FileInfo::exists(const QString &filepath) {
  return QFileInfo(filepath).exists();
}

QString FileInfo::lastDirectory() { return s_previousDirectory; }
