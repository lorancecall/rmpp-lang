// Settings > Languages page, shown over the Settings payload area when its sidebar row is selected.
// A list of the installed languages; tapping one opens its page (on/off, keyboard, font, priority). The font is
// picked in a popup that shows each family in its own face.
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
    // List order: languages in use by priority (shared Han characters use the first one's font), then the rest.
    readonly property var ordered: {
        const on = entries.filter(e => e.enabled && e.priority >= 0).sort((a, b) => a.priority - b.priority);
        return on.concat(entries.filter(e => !(e.enabled && e.priority >= 0)));
    }
    property string current: ""          // language code whose page is open, "" = the list
    property bool picking: false         // font popup open
    readonly property var entry: {
        for (let i = 0; i < entries.length; ++i) if (entries[i].code === current) return entries[i];
        return null;
    }


    function send(cmd) { lang.command = cmd + "#" + Date.now(); }
    // Languages in use that have their own font can be reordered (English and typed text cannot).
    function rowEntry(row) { return row && row.entry ? row.entry : ({}); }
    function movable(e) { return !!e.enabled && !e.fixed && e.priority >= 0; }
    function neighbour(e, dir) {
        return ordered.some(o => movable(o) && (dir < 0 ? o.priority < e.priority : o.priority > e.priority));
    }
    function summary(e) {
        if (e.code === "latin") return "Font: " + (e.font || "Default (reMarkable Sans)");
        if (e.code === "en") return "Always on";
        if (!e.enabled) return "Off";
        const parts = [];
        if (e.hasKeyboard) parts.push("Keyboard " + (e.keyboard ? "on" : "off"));
        parts.push("Font: " + (e.font || "Default"));
        return parts.join(" · ");
    }
    // "" = Default: show the device's own choice, not the current fallback.
    function sampleFont(e, fam) { return fam || e.deviceDefault || "reMarkable Sans"; }

    x: payloadLoaderRef.x
    y: payloadLoaderRef.y
    width: payloadLoaderRef.width
    height: payloadLoaderRef.height
    z: 10
    color: "white"
    visible: settingsRoot._selectedPage === langPage
    onVisibleChanged: if (!visible) { current = ""; picking = false; }

    ArkControls.PlaceholderBar {
        id: placeholder
        anchors { top: parent.top; left: parent.left; right: parent.right }
    }

    // ---- list of languages ----
    Flickable {
        visible: page.current === ""
        anchors {
            top: placeholder.bottom; left: parent.left; right: parent.right; bottom: parent.bottom
            leftMargin: ArkControls.Values.margin.horizontal.large
            rightMargin: ArkControls.Values.margin.horizontal.large
        }
        contentHeight: listColumn.implicitHeight + ArkControls.Values.margin.vertical.large
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: listColumn
            width: parent.width
            spacing: ArkControls.Values.spacing.large

            ArkControls.Title {
                type: ArkControls.Title.Large
                text: "Languages"
                Layout.fillWidth: true
            }
            Repeater {
                model: page.ordered
                delegate: ArkControls.Panel {
                    required property var modelData
                    label: modelData.code === "latin" || modelData.code === "en"
                           ? modelData.name : modelData.native + " · " + modelData.name
                    description: page.summary(modelData)
                    Layout.fillWidth: true
                    action: Row {
                        readonly property var entry: modelData
                        spacing: 16
                        // Order arrows: smaller than the "›" button next to them.
                        Repeater {
                            model: [-1, 1]
                            delegate: Rectangle {
                                required property int modelData
                                readonly property bool live: page.neighbour(page.rowEntry(parent), modelData)
                                visible: page.movable(page.rowEntry(parent))
                                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                                width: 60; height: 60; radius: 6
                                color: "white"
                                border.width: 2
                                border.color: live ? "black" : "#b0b0b0"
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData < 0 ? "▲" : "▼"
                                    font.pixelSize: 24
                                    color: parent.live ? "black" : "#b0b0b0"
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -8
                                    enabled: parent.live
                                    onClicked: page.send((parent.modelData < 0 ? "up:" : "down:") + page.rowEntry(parent.parent).code)
                                }
                            }
                        }
                        ArkControls.Button {
                            text: "›"
                            type: ArkTokens.Button.secondary
                            visible: modelData.code !== "en"
                            onClicked: page.current = modelData.code
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: modelData.code !== "en"
                        onClicked: page.current = modelData.code
                        z: -1
                    }
                }
            }
            ArkControls.Panel {
                label: "Order"
                description: "▲ ▼ set the order in which keyboards switch (Ctrl+Space, right Alt, the language button)."
                Layout.fillWidth: true
                Layout.topMargin: ArkControls.Values.spacing.large
            }
            ArkControls.Panel {
                label: "Add a language or a font"
                description: "Languages are add-ons: choose them in the rmpp installer. Fonts: rmpp-lang font add "
                             + "MyFont.ttf --lang CODE from your computer, or copy .ttf / .otf files into "
                             + "/home/root/.local/share/fonts/rmpp-lang/ followed by the language code (ko, ja, zh, latin, ...)."
                Layout.fillWidth: true
                Layout.topMargin: ArkControls.Values.spacing.large
            }
        }
    }

    // ---- one language ----
    Flickable {
        visible: page.current !== "" && page.entry !== null
        anchors {
            top: placeholder.bottom; left: parent.left; right: parent.right; bottom: parent.bottom
            leftMargin: ArkControls.Values.margin.horizontal.large
            rightMargin: ArkControls.Values.margin.horizontal.large
        }
        contentHeight: detail.implicitHeight + ArkControls.Values.margin.vertical.large
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: detail
            readonly property var e: page.entry || ({ code: "", families: [], layouts: [] })
            width: parent.width
            spacing: ArkControls.Values.spacing.large

            ArkControls.Button {
                text: "‹  Languages"
                type: ArkTokens.Button.secondary
                onClicked: page.current = ""
            }
            ArkControls.Title {
                type: ArkControls.Title.Large
                text: detail.e.code === "latin" ? (detail.e.name || "") : (detail.e.native || "") + " · " + (detail.e.name || "")
                Layout.fillWidth: true
            }
            ArkControls.Panel {
                visible: !detail.e.fixed
                label: "Use " + (detail.e.name || "")
                description: detail.e.enabled ? "On" : "Off"
                Layout.fillWidth: true
                action: ArkControls.Toggle {
                    checked: !!detail.e.enabled
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -20
                        onClicked: page.send("enable:" + detail.e.code + ":" + (detail.e.enabled ? "0" : "1"))
                    }
                }
            }
            ArkControls.Panel {
                visible: !!detail.e.enabled && detail.e.code !== "latin"
                label: "Keyboard"
                description: detail.e.hasKeyboard
                    ? (detail.e.keyboardName ? detail.e.keyboardName + " on the on-screen keyboard"
                                             : "On-screen layout " + detail.e.layouts.join(", "))
                      + " (turn it on in Language & keyboard). Ctrl+Space or right Alt on Type Folio, Bluetooth "
                      + "and USB keyboards." + (detail.e.rtl ? " Right-to-left editing is not checked yet." : "")
                    : "Not yet: typing this language needs a conversion input method."
                Layout.fillWidth: true
                action: ArkControls.Toggle {
                    visible: !!detail.e.hasKeyboard
                    checked: !!detail.e.keyboard
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -20
                        onClicked: page.send("keyboard:" + detail.e.code + ":" + (detail.e.keyboard ? "0" : "1"))
                    }
                }
            }
            ArkControls.Panel {
                visible: !!detail.e.enabled
                label: "Font: " + (detail.e.font || "Default")
                description: detail.e.code === "latin"
                    ? "Experimental: the font of typed text in notebooks. Other devices and the reMarkable apps keep "
                      + "their own font."
                    : "Used wherever this language's script appears: typed text, the keyboard and the rest of the tablet."
                Layout.fillWidth: true
                action: ArkControls.Button {
                    text: "Change"
                    type: ArkTokens.Button.secondary
                    enabled: (detail.e.families || []).length > 0
                    onClicked: page.picking = true
                }
            }
            Text {
                visible: !!detail.e.enabled
                text: detail.e.sample || ""
                font.family: page.sampleFont(detail.e, detail.e.font)
                font.pixelSize: 40
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.leftMargin: 8
            }
            ArkControls.Panel {
                visible: !!detail.e.enabled
                label: "Your fonts for " + (detail.e.name || "")
                description: detail.e.fontDir || ""
                Layout.fillWidth: true
            }
        }
    }

    // ---- font popup ----
    MouseArea {                           // dims nothing (e-paper), only closes on a tap outside
        anchors.fill: parent
        visible: page.picking
        z: 20
        onClicked: page.picking = false
    }
    Rectangle {
        visible: page.picking && page.entry !== null
        z: 21
        width: parent.width * 0.8
        height: Math.min(parent.height * 0.8, pickColumn.implicitHeight + 64)
        anchors.centerIn: parent
        color: "white"
        border.color: "black"
        border.width: 3
        radius: 12

        Flickable {
            anchors.fill: parent
            anchors.margins: 32
            contentHeight: pickColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: pickColumn
                width: parent.width
                spacing: ArkControls.Values.spacing.medium

                ArkControls.Title {
                    type: ArkControls.Title.Medium
                    text: "Font for " + (page.entry ? page.entry.name : "")
                    Layout.fillWidth: true
                }
                Repeater {
                    model: page.entry ? [""].concat(page.entry.families) : []
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool chosen: page.entry && page.entry.font === modelData
                        Layout.fillWidth: true
                        implicitHeight: 128
                        color: chosen ? "black" : "white"
                        border.color: "black"
                        border.width: 2
                        radius: 8
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 24
                            width: parent.width - 48
                            spacing: 6
                            Text {
                                text: modelData || "Default"
                                color: chosen ? "white" : "black"
                                font.pixelSize: 28
                                font.bold: true
                            }
                            Text {
                                width: parent.width
                                text: (page.entry && page.entry.sample) || "Aa"
                                color: chosen ? "white" : "black"
                                font.family: page.sampleFont(page.entry || {}, modelData)
                                font.pixelSize: 34
                                elide: Text.ElideRight
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: { page.send("font:" + page.entry.code + ":" + modelData); page.picking = false; }
                        }
                    }
                }
                ArkControls.Button {
                    text: "Cancel"
                    type: ArkTokens.Button.secondary
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: page.picking = false
                }
            }
        }
    }
}
