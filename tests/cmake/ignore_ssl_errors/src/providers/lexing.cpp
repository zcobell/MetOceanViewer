// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// What the line scanner must get right; run.cmake names each case.

/// Never calls ignoreSslErrors, retries in [0, n)
void unbalanced() { reply->ignoreSslErrors(); }

// §6.2: never ignoreSslErrors()
int section = 0;  // §6.2: never VerifyNone

void comment_in_string() { call("/*", reply->ignoreSslErrors()); }  // */

void quote_literal() {
  const char quote = '"';  // never VerifyNone
}

void query_peer() { configuration.setPeerVerifyMode(QSslSocket::QueryPeer); }
