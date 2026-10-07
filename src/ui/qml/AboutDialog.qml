// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

import QtQuick
import QtQuick.Controls.Basic

// Placeholder About box. The version comes from project(VERSION) through
// QCoreApplication::applicationVersion (mov::ui::set_application_metadata),
// which QtQuick exposes as Application.version.
Dialog {
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("About %1").arg(Application.displayName)
    standardButtons: Dialog.Close

    Label {
        text: qsTr("Version %1").arg(Application.version)
    }
}
