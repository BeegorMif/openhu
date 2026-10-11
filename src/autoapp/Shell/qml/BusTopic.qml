import QtQuick

// Current value of an EventBus topic, e.g. BusTopic { id: media; topic: "media.metadata" } -> media.data.title
QtObject {
    id: busTopic

    property string topic
    property var data: eventBus.last(topic)

    property Connections _connections: Connections {
        target: eventBus
        function onEvent(topic, data) {
            if (topic === busTopic.topic)
                busTopic.data = data
        }
    }
}
