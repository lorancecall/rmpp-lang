// Sidebar row injected into Settings (after xochitl's own language row when found).
// Context properties: lang (QQmlPropertyMap), settingsRoot (Settings.qml root item).
import QtQuick
import QtQuick.Layouts
import ark.tokens as ArkTokens
import ark.controls as ArkControls

ArkControls.SidebarItem {
    readonly property int langPage: 1003

    objectName: "rmppLangSidebarItem"
    type: ArkTokens.Sidebar.primary
    text: settingsRoot.showLabels ? "Languages" : ""
    iconSource: "qrc:/rmpplang/icons/language.svg"
    highlighted: settingsRoot._selectedPage === langPage
    focusPolicy: Qt.StrongFocus
    Layout.fillWidth: true

    onTriggered: settingsRoot._selectedPage = langPage
}
