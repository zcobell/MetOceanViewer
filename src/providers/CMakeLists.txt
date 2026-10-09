# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# mov_providers: the Qt edge of the fetches (docs/providers-design.md §6):
# the network, the transport, the jobs that drive fetch's machine, the
# providers, station catalogs, the update check and describe(). QFuture and
# QString are in its interface; Qt Network and Qt Concurrent stay private.
# Built with the Qt layers (MOV_ENABLE_QT).

mov_add_module(
    providers
    SOURCES user_agent.cpp
    PUBLIC_LINK mov::fetch Qt6::Core
    PRIVATE_LINK Qt6::Network Qt6::Concurrent
)
