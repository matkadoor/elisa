/*
   SPDX-FileCopyrightText: 2016 (c) Matthieu Gallien <matthieu_gallien@yahoo.fr>

   SPDX-License-Identifier: LGPL-3.0-or-later
 */

import QtQuick 2.7
import QtQuick.Layouts 1.2
import QtQuick.Controls 2.3
import org.kde.kirigami 2.5 as Kirigami

import org.kde.elisa 1.0

import QtQuick 2.0

Rectangle {
    id: background

    property var headerData
    property string album: headerData[0]
    property string albumArtist: headerData[1]
    property url imageUrl: headerData[2]
    property alias textColor: mainLabel.color
    property alias backgroundColor: background.color

    implicitHeight: contentLayout.implicitHeight

    color: myPalette.midlight

    RowLayout {
        id: contentLayout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        // No bottom anchor so it can grow vertically

        spacing: Kirigami.Units.smallSpacing

        ImageWithFallback {
            Layout.preferredWidth: elisaTheme.playListAlbumArtSize
            Layout.preferredHeight: elisaTheme.playListAlbumArtSize
            Layout.margins: Kirigami.Units.largeSpacing

            source: imageUrl
            fallback: elisaTheme.defaultAlbumImage

            sourceSize.width: elisaTheme.playListAlbumArtSize
            sourceSize.height: elisaTheme.playListAlbumArtSize

            fillMode: Image.PreserveAspectFit
            asynchronous: true
        }

        ColumnLayout {
            id: albumHeaderTextColumn

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: !LayoutMirroring.enabled ? - Kirigami.Units.smallSpacing : 0
            Layout.rightMargin: LayoutMirroring.enabled ? - Kirigami.Units.smallSpacing : 0
            Layout.topMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing

            spacing: Kirigami.Units.smallSpacing

            LabelWithToolTip {
                id: mainLabel

                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom | Qt.AlignLeft

                text: album
                level: 2
                font.weight: Font.Bold

                elide: Text.ElideRight
                wrapMode: Text.WordWrap
                maximumLineCount: 2
            }

            LabelWithToolTip {
                id: authorLabel

                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop | Qt.AlignLeft

                text: albumArtist
                color: mainLabel.color

                elide: Text.ElideRight
                wrapMode: Text.WordWrap
                maximumLineCount: 2
            }
        }
    }
}
