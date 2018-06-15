import QtQuick.Window 2.2
import QtQuick 2.7
import QtQuick.Controls 1.4
import QtLocation 5.9
import QtPositioning 5.5

MapViewer {
    id: mapView
    Plugin {
        id: mapPlugin
        name: "mapbox"
        PluginParameter { name: "mapbox.access_token"; value: mapboxKey }
    }
}
