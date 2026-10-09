// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Both ways of turning certificate checks off; run.cmake expects each
// reported.
void insecure() {
  reply->ignoreSslErrors();
  configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
}
