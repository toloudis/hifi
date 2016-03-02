//
//  RunningScripts.qml
//
//  Created by Bradley Austin Davis on 12 Jan 2016
//  Copyright 2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Dialogs 1.2 as OriginalDialogs
import Qt.labs.settings 1.0

import "../../styles-uit"
import "../../controls-uit" as HifiControls
import "../../windows-uit"

Window {
    id: root
    objectName: "RunningScripts"
    title: "Running Scripts"
    resizable: true
    destroyOnInvisible: true
    x: 40; y: 40
    implicitWidth: 384; implicitHeight: 640
    minSize: Qt.vector2d(200, 300)

    HifiConstants { id: hifi }

    property var scripts: ScriptDiscoveryService;
    property var scriptsModel: scripts.scriptsModelFilter
    property var runningScriptsModel: ListModel { }

    Settings {
        category: "Overlay.RunningScripts"
        property alias x: root.x
        property alias y: root.y
    }

    Connections {
        target: ScriptDiscoveryService
        onScriptCountChanged: updateRunningScripts();
    }

    Component.onCompleted: updateRunningScripts()

    function setDefaultFocus() {
        // Work around FocusScope of scrollable window.
        filterEdit.forceActiveFocus();
    }

    function updateRunningScripts() {
        var runningScripts = ScriptDiscoveryService.getRunning();
        runningScriptsModel.clear()
        for (var i = 0; i < runningScripts.length; ++i) {
            runningScriptsModel.append(runningScripts[i]);
        }
    }

    function loadScript(script) {
        console.log("Load script " + script);
        scripts.loadOneScript(script);
    }

    function reloadScript(script) {
        console.log("Reload script " + script);
        scripts.stopScript(script, true);
    }

    function stopScript(script) {
        console.log("Stop script " + script);
        scripts.stopScript(script);
    }

    function reloadAll() {
        console.log("Reload all scripts");
        scripts.reloadAllScripts();
    }

    function stopAll() {
        console.log("Stop all scripts");
        scripts.stopAllScripts();
    }

    Column {
        width: pane.contentWidth

        HifiControls.StaticSection {
            name: "Currently Running"

            Row {
                spacing: hifi.dimensions.contentSpacing.x

                HifiControls.Button {
                    text: "Reload all"
                    color: hifi.buttons.black
                    onClicked: reloadAll()
                }

                HifiControls.Button {
                    text: "Stop all"
                    color: hifi.buttons.red
                    onClicked: stopAll()
                }
            }

            HifiControls.Table {
                tableModel: runningScriptsModel
                height: 185
                colorScheme: hifi.colorSchemes.dark
                anchors.left: parent.left
                anchors.right: parent.right
            }
        }

        HifiControls.StaticSection {
            name: "Load Scripts"
            hasSeparator: true

            Row {
                spacing: hifi.dimensions.contentSpacing.x
                anchors.right: parent.right

                HifiControls.Button {
                    text: "from URL"
                    color: hifi.buttons.black
                    height: 26
                    onClicked: fromUrlTimer.running = true

                    // For some reason trigginer an API that enters
                    // an internal event loop directly from the button clicked
                    // trigger below causes the appliction to behave oddly.
                    // Most likely because the button onClicked handling is never
                    // completed until the function returns.
                    // FIXME find a better way of handling the input dialogs that
                    // doesn't trigger this.
                    Timer {
                        id: fromUrlTimer
                        interval: 5
                        repeat: false
                        running: false
                        onTriggered: ApplicationInterface.loadScriptURLDialog();
                    }
                }

                HifiControls.Button {
                    text: "from Disk"
                    color: hifi.buttons.black
                    height: 26
                    onClicked: fromDiskTimer.running = true

                    Timer {
                        id: fromDiskTimer
                        interval: 5
                        repeat: false
                        running: false
                        onTriggered: ApplicationInterface.loadDialog();
                    }
                }
            }

            HifiControls.TextField {
                id: filterEdit
                anchors.left: parent.left
                anchors.right: parent.right
                focus: true
                colorScheme: hifi.colorSchemes.dark
                //placeholderText: "filter"
                label: "Filter"
                onTextChanged: scriptsModel.filterRegExp =  new RegExp("^.*" + text + ".*$", "i")
                Component.onCompleted: scriptsModel.filterRegExp = new RegExp("^.*$", "i")
            }

            HifiControls.Tree {
                id: treeView
                height: 155
                treeModel: scriptsModel
                colorScheme: hifi.colorSchemes.dark
                anchors.left: parent.left
                anchors.right: parent.right
            }

            HifiControls.TextField {
                id: selectedScript
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.rightMargin: loadButton.width + hifi.dimensions.contentSpacing.x

                colorScheme: hifi.colorSchemes.dark
                readOnly: true

                Connections {
                    target: treeView
                    onCurrentIndexChanged: {
                        var path = scriptsModel.data(treeView.currentIndex, 0x100)
                        if (path) {
                            selectedScript.text = path
                        } else {
                            selectedScript.text = ""
                        }
                    }
                }
            }

            Item {
                // Take the loadButton out of the column flow.
                id: loadButtonContainer
                anchors.top: selectedScript.top
                anchors.right: parent.right

                HifiControls.Button {
                    id: loadButton
                    anchors.right: parent.right

                    text: "Load"
                    color: hifi.buttons.blue
                    enabled: selectedScript.text != ""
                    onClicked: root.loadScript(selectedScript.text)
                }
            }

            HifiControls.VerticalSpacer { }
        }
    }
}

