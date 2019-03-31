import QtQuick 2.12
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3
import QtQuick.Dialogs 1.3
import com.jgaa.darkspeak 1.0


Page {
    width: 600
    height: 400

    header: Header {
        whom: identities.current
        text: qsTr("Conversations for ")
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.top: parent.top
        anchors.topMargin: 4
    }

    Connections {
        target: conversations
        onModelReset: {
            console.log("onModelReset");
            if (conversations.rowCount() === 1) {
                // Only one contact, so just select it as the current one
                list.currentIndex = 0;
            } else {
                list.currentIndex = -1;
            }
        }

        onCurrentRowChanged: {
            list.currentIndex = conversations.currentRow
            messages.setConversation(conversations.current)
        }
    }

    ListView {
        id: list
        interactive: true
        model: conversations
        anchors.fill: parent
        highlight: highlightBar

        onCurrentIndexChanged: {
            conversations.currentRow = list.currentIndex
        }

        delegate: Item {
            id: itemDelegate
            width: parent.width
            height: 60
            property Conversation cco : conversation
            Row {
                id: row1
                anchors.fill: parent
                spacing: 4

                RoundedImage {
                    source: cco.participant.avatarUrl
                    height: 45
                    width: 45
                    anchors.left: parent.left
                    anchors.leftMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    border.width: 2
                    border.color: cco.participant.online ? "lime" : "red"
                    color: "black"
                }


//                Rectangle {
//                    id: avatarFrame
//                    height: 45
//                    width: 45
//                    anchors.left: parent.left
//                    anchors.leftMargin: 4
//                    anchors.verticalCenter: parent.verticalCenter
//                    border.width: 1
//                    radius: width*0.5
//                    border.color: cco.participant.online ? "lime" : "red"

//                    Image {
//                        id: img
//                        fillMode: Image.PreserveAspectFit
//                        height: 36
//                        anchors.horizontalCenter: parent.horizontalCenter
//                        anchors.verticalCenter: parent.verticalCenter
//                        width: 36
//                        source: cco.participant.avatarUrl
//                        cache: false
//                        property bool rounded: true
//                        property bool adapt: true
//                        layer.enabled: rounded
//                        layer.effect: OpacityMask {
//                            maskSource: Item {
//                                width: img.width
//                                height: img.height
//                                Rectangle {
//                                    anchors.centerIn: parent
//                                    width: img.adapt ? img.width : Math.min(img.width, img.height)
//                                    height: img.adapt ? img.height : width
//                                    radius: Math.min(width, height)
//                                }
//                            }
//                        }
//                    }
//                }

                Column {
                    spacing: 10
                    x: 116
                    Text {
                        font.pointSize: 10
                        color: "white"
                        text: cco.name ? cco.name : cco.participant.name ? cco.participant.name : cco.participant.nickName
                        font.bold: itemDelegate.ListView.isCurrentItem
                    }

                    Text {
                        font.pointSize: 14
                        color: "skyblue"
                        text: cco.topic
                        font.bold: itemDelegate.ListView.isCurrentItem
                    }
                }
            }

            MouseArea {
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                anchors.fill: parent
                onClicked: {
                    list.currentIndex = index
                    if (mouse.button === Qt.RightButton) {
                        contextMenu.x = mouse.x;
                        contextMenu.y = mouse.y;
                        contextMenu.open();
                    }
                }

                onPressAndHold: {
                    list.currentIndex = index
                    contextMenu.x = mouse.x;
                    contextMenu.y = mouse.y;
                    contextMenu.open();
                }
            }
        }

    }

    Component {
         id: highlightBar
         Rectangle {
             radius: 5
             y: list.currentItem.y;
             color: "midnightblue"
             border.color: "aquamarine"
             Behavior on y { SpringAnimation { spring: 1; damping: 0.1 } }
         }
    }



}
