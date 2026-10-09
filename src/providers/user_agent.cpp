// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/providers/user_agent.hpp"

#include <QByteArrayView>
#include <QString>

#include "mov/core/version.hpp"

namespace mov::providers {

QString default_user_agent() {
  return QStringLiteral(
             "MetOceanViewer/%1 (+https://github.com/zcobell/MetOceanViewer)")
      .arg(QString::fromUtf8(QByteArrayView{core::version()}));
}

}  // namespace mov::providers
