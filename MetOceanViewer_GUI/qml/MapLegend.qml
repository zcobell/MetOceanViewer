import QtQuick 2.0

Rectangle {

    property double c0: -5
    property double c1: -3.5
    property double c2: -1.5
    property double c3: 0.0
    property double c4: 1.5
    property double c5: 3.5
    property double c6: 5.0
    property string units: "ft"
    property string legendText: "<h1><b>Legend</b></h1><br>
                                <b>Modeled less Observed ("+units+")</b><br>
                                <img src=\"qrc:/rsc/img/mm_20_purple.png\"> &lt; "+c0.toFixed(2)+"<br>
                                <img src=\"qrc:/rsc/img/mm_20_darkblue.png\"> "+c0.toFixed(2)+" - "+c1.toFixed(2)+"<br>
                                <img src=\"qrc:/rsc/img/mm_20_lightblue.png\"> "+c1.toFixed(2)+" - "+c2.toFixed(2)+"<br>
                                <img src=\"qrc:/rsc/img/mm_20_darkgreen.png\"> "+c2.toFixed(2)+" - "+c3.toFixed(2)+"<br>
                                <img src=\"qrc:/rsc/img/mm_20_lightgreen.png\"> "+c3.toFixed(2)+" - "+c4.toFixed(2)+"<br>
                                <img src=\"qrc:/rsc/img/mm_20_yellow.png\"> "+c4.toFixed(2)+" - "+c5.toFixed(2)+"<br>
                                <img src=\"qrc:/rsc/img/mm_20_darkorange.png\"> "+c5.toFixed(2)+" - "+c6.toFixed(2)+"<br>
                                <img src=\"qrc:/rsc/img/mm_20_red.png\"> &gt; "+c6.toFixed(2)+"<br>
                                <img src=\"qrc:/rsc/img/mm_20_white.png\"> Dry in model"
    anchors.right: parent.right
    anchors.top: parent.top
    border.color: "black"
    border.width: 3
    width: 160
    height: 300
    z: 100


    MouseArea{
        anchors.fill: parent
    }

    parent: map

    TextEdit {
        parent: legend
        anchors.bottom: parent.bottom
        anchors.top: parent.top
        anchors.left: parent.left
        readOnly: true
        textFormat: Text.RichText
        text: legendText
        anchors.margins: 10
        font.family: "Arial"
        font.pointSize: 8
        //selectByMouse: true
        //mouseSelectionMode: TextEdit.SelectWords
    }
}
