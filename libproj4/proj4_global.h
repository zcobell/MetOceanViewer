#ifndef PROJ4_GLOBAL_H
#define PROJ4_GLOBAL_H

#include <QtCore/qglobal.h>

#if defined(PROJ4_LIBRARY)
#  define PROJ4SHARED_EXPORT Q_DECL_EXPORT
#else
#  define PROJ4SHARED_EXPORT Q_DECL_IMPORT
#endif

#endif // PROJ4_GLOBAL_H
