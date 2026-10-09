// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// No exception, not even the test driver.
#pragma once

inline void insecure_test() { reply->ignoreSslErrors(); }
