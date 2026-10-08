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
#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

#include "mov/core/version.hpp"
#include "mov/io/projection.hpp"
#include "mov/ui/app_identity.hpp"

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

[[nodiscard]] QString to_qstring(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}  // namespace

void select_graphics_api() {
  // MapLibre draws into a texture of the API it was built for; Qt Quick must
  // use the same one (its default on Windows is Direct3D).
  QQuickWindow::setGraphicsApi(
      to_graphics_api(QMapLibre::supportedRendererType()));
}

void set_application_metadata() {
  QGuiApplication::setApplicationName(to_qstring(app_identity::name));
  QGuiApplication::setApplicationDisplayName(to_qstring(app_identity::name));
  QGuiApplication::setApplicationVersion(QString::fromUtf8(core::version()));
  // The organization name and domain namespace QSettings; the desktop file
  // name is the Linux .desktop entry's basename (the application id).
  QGuiApplication::setOrganizationName(to_qstring(app_identity::name));
  QGuiApplication::setOrganizationDomain(QStringLiteral("zcobell.github.io"));
  QGuiApplication::setDesktopFileName(to_qstring(app_identity::id));
  // qt_add_qml_module puts RESOURCES under /qt/qml/<URI path>/ (the default
  // resource prefix of qt_standard_project_setup(REQUIRES 6.5+)), keeping
  // their path relative to src/ui/qml.
  QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/") +
                                       to_qstring(app_identity::qml_module) +
                                       QStringLiteral("/images/app-icon.svg")));
}

std::filesystem::path packaged_projection_data_dir() {
  const QDir dir(QCoreApplication::applicationDirPath() + u'/' +
                 to_qstring(app_identity::proj_data_dir));
  return dir.filesystemAbsolutePath();
}

std::optional<std::filesystem::path> configure_projection_data() {
  std::filesystem::path dir = packaged_projection_data_dir();
  std::error_code ignored;
  if (not std::filesystem::is_regular_file(dir / "proj.db", ignored)) {
    return std::nullopt;
  }
  io::set_projection_data_dir(dir);
  return dir;
}

bool load_main_window(QQmlApplicationEngine& engine) {
  engine.loadFromModule(to_qstring(app_identity::qml_module),
                        to_qstring(app_identity::main_window_type));
  return not engine.rootObjects().isEmpty();
}

}  // namespace mov::ui
