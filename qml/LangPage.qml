// Settings > Languages page, shown over the Settings payload area when its sidebar row is selected.
// Context properties: lang (QQmlPropertyMap: "languages" JSON, "command" written here), settingsRoot, payloadLoaderRef.
import QtQuick
import QtQuick.Layouts
import ark.tokens as ArkTokens
import ark.controls as ArkControls

Rectangle {
    id: page
    readonly property int langPage: 1003
    readonly property var entries: {
        try { return JSON.parse(lang.languages || "[]"); } catch (e) { return []; }
    }
    readonly property var samples: ({
        "ko": "다람쥐 헌 쳇바퀴에 타고파 · 漢字",
        "en": "The quick brown fox jumps over the lazy dog",
        "ja": "いろはにほへと ちりぬるを · 漢字",
        "zh": "天地玄黄 宇宙洪荒 · 汉字",
        "latin": "The quick brown fox jumps over the lazy dog"
    })

    function send(cmd) { lang.command = cmd + "#" + Date.now(); }
    // Next choice after the current one: "" (default) and then each family.
    function nextFont(e) {
        const all = [""].concat(e.families);
        const i = all.indexOf(e.font);
        return all[(i + 1) % all.length];
    }

    x: payloadLoaderRef.x
    y: payloadLoaderRef.y
    width: payloadLoaderRef.width
    height: payloadLoaderRef.height
    z: 10
    color: "white"
    visible: settingsRoot._selectedPage === langPage

    ArkControls.PlaceholderBar {
        id: placeholder
        anchors { top: parent.top; left: parent.left; right: parent.right }
    }

    Flickable {
        anchors {
            top: placeholder.bottom; left: parent.left; right: parent.right; bottom: parent.bottom
            leftMargin: ArkControls.Values.margin.horizontal.large
            rightMargin: ArkControls.Values.margin.horizontal.large
        }
        contentHeight: column.implicitHeight + ArkControls.Values.margin.vertical.large
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: column
            width: parent.width
            spacing: ArkControls.Values.spacing.large

            ArkControls.Title {
                type: ArkControls.Title.Large
                text: "Languages"
                Layout.fillWidth: true
            }
            ArkControls.Panel {
                label: "Languages you write in"
                description: "Each language can have its own keyboard and font. Fonts apply to typed text, "
                             + "the keyboard and the rest of the tablet wherever the language's script appears."
                Layout.fillWidth: true
            }

            Repeater {
                model: page.entries
                delegate: ColumnLayout {
                    required property var modelData
                    required property int index
                    readonly property var e: modelData
                    spacing: ArkControls.Values.spacing.medium
                    Layout.fillWidth: true

                    ArkControls.Title {
                        type: ArkControls.Title.Medium
                        text: e.code === "latin" ? e.name : e.name + " · " + e.native
                        Layout.fillWidth: true
                        Layout.topMargin: ArkControls.Values.spacing.large
                    }
                    ArkControls.Panel {
                        visible: !e.fixed
                        label: e.name
                        description: e.enabled ? "On" : "Off"
                        Layout.fillWidth: true
                        action: ArkControls.Toggle {
                            checked: !!e.enabled
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -20
                                onClicked: page.send("enable:" + e.code + ":" + (e.enabled ? "0" : "1"))
                            }
                        }
                    }
                    ArkControls.Panel {
                        visible: !!e.enabled && e.code !== "en" && e.code !== "latin"
                        label: "Keyboard"
                        description: e.hasKeyboard
                            ? (e.code === "ko" ? "2-beolsik on the on-screen keyboard (turn on Korean in Language & keyboard), "
                                                 + "Ctrl+Space or right Alt on Type Folio, Bluetooth and USB keyboards"
                                               : "")
                            : "Not yet: typing this language needs a conversion input method"
                        Layout.fillWidth: true
                        action: ArkControls.Toggle {
                            visible: !!e.hasKeyboard
                            checked: !!e.keyboard
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -20
                                onClicked: page.send("keyboard:" + e.code + ":" + (e.keyboard ? "0" : "1"))
                            }
                        }
                    }
                    ArkControls.Panel {
                        visible: !!e.enabled && e.code !== "en"
                        label: "Font: " + (e.font || "Default")
                        description: e.code === "latin"
                            ? "Experimental: replaces the font of typed text in notebooks. Other devices and the "
                              + "reMarkable apps keep showing their own font."
                            : (e.families.length ? e.families.length + " available. Add more in " + e.fontDir
                                                   : "Add fonts in " + e.fontDir)
                        Layout.fillWidth: true
                        action: ArkControls.Button {
                            text: "Change"
                            type: ArkTokens.Button.secondary
                            enabled: e.families.length > 0
                            onClicked: page.send("font:" + e.code + ":" + page.nextFont(e))
                        }
                    }
                    Text {
                        visible: !!e.enabled && e.code !== "en"
                        text: page.samples[e.code] || ""
                        font.family: e.font || (e.code === "latin" ? "reMarkable Serif Small" : "reMarkable Sans")
                        font.pixelSize: 40
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                    }
                }
            }

            ArkControls.Title {
                type: ArkControls.Title.Medium
                text: "Adding fonts"
                Layout.fillWidth: true
                Layout.topMargin: ArkControls.Values.spacing.large
            }
            ArkControls.Panel {
                label: "From your computer: rmpp-lang font add MyFont.ttf --lang ko"
                description: "Or copy .ttf / .otf files into /home/root/.local/share/fonts/rmpp-lang/<language>/ "
                             + "(ko, ja, zh, latin). New fonts show up here within a few seconds."
                Layout.fillWidth: true
            }
        }
    }
}
