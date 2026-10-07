// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/ui/startup.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QMapLibre/Utils>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QString>
#include <utility>

#include "mov/core/version.hpp"
#include "mov/io/projection.hpp"

namespace mov::ui {

namespace {

QSGRendererInterface::GraphicsApi to_graphics_api(
    const QMapLibre::RendererType renderer) {
  switch (renderer) {
    case QMapLibre::OpenGL:
      return QSGRendererInterface::OpenGL;
    case QMapLibre::Vulkan:
      return QSGRendererInterface::Vulkan;
    case QMapLibre::Metal:
      return QSGRendererInterface::Metal;
  }
  std::unreachable();
}

}  // namespace

void select_graphics_api() {
  // MapLibre draws into a texture of the API it was built for; Qt Quick must
  // use the same one (its default on Windows is Direct3D).
  QQuickWindow::setGraphicsApi(
      to_graphics_api(QMapLibre::supportedRendererType()));
}

void set_application_metadata() {
  QGuiApplication::setApplicationName(QStringLiteral("MetOceanViewer"));
  QGuiApplication::setApplicationDisplayName(QStringLiteral("MetOceanViewer"));
  QGuiApplication::setApplicationVersion(QString::fromUtf8(core::version()));
  // Reverse-DNS identity of plan §6.19: io.github.zcobell.metoceanviewer.
  QGuiApplication::setOrganizationName(QStringLiteral("MetOceanViewer"));
  QGuiApplication::setOrganizationDomain(QStringLiteral("zcobell.github.io"));
  QGuiApplication::setDesktopFileName(
      QStringLiteral("io.github.zcobell.metoceanviewer"));
  // qt_add_qml_module puts RESOURCES under /qt/qml/<URI path>/ (the default
  // resource prefix of qt_standard_project_setup(REQUIRES 6.5+)), keeping
  // their path relative to src/ui/qml.
  QGuiApplication::setWindowIcon(
      QIcon(QStringLiteral(":/qt/qml/MetOceanViewer/images/app-icon.svg")));
}

void configure_projection_data() {
  // MOV_APP_PROJ_DATA_DIR comes from cmake/Packaging.cmake.
  const QDir dir(QCoreApplication::applicationDirPath() + u'/' +
                 QStringLiteral(MOV_APP_PROJ_DATA_DIR));
  if (dir.exists(QStringLiteral("proj.db"))) {
    io::set_projection_data_dir(dir.filesystemAbsolutePath());
  }
}

bool load_main_window(QQmlApplicationEngine& engine) {
  engine.loadFromModule("MetOceanViewer", "Main");
  return not engine.rootObjects().isEmpty();
}

}  // namespace mov::ui
