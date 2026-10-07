// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/ui/startup.hpp"

#include <QGuiApplication>
#include <QIcon>
#include <QMapLibre/Utils>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QString>
#include <utility>

#include "mov/core/version.hpp"

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
  QGuiApplication::setOrganizationDomain(QStringLiteral("zcobell.github.io"));
  QGuiApplication::setDesktopFileName(
      QStringLiteral("io.github.zcobell.metoceanviewer"));
  QGuiApplication::setWindowIcon(
      QIcon(QStringLiteral(":/qt/qml/MetOceanViewer/images/app-icon.svg")));
}

bool load_main_window(QQmlApplicationEngine& engine) {
  engine.loadFromModule("MetOceanViewer", "Main");
  return not engine.rootObjects().isEmpty();
}

}  // namespace mov::ui
