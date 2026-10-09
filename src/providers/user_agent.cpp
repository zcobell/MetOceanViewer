// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/providers/user_agent.hpp"

#include <QString>
#include <QtTypes>
#include <string_view>

#include "mov/core/version.hpp"

namespace mov::providers {

QString default_user_agent() {
  const std::string_view version = core::version();
  return QStringLiteral(
             "MetOceanViewer/%1 (+https://github.com/zcobell/MetOceanViewer)")
      .arg(QString::fromUtf8(version.data(),
                             static_cast<qsizetype>(version.size())));
}

}  // namespace mov::providers
