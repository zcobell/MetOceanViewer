/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2018  Zach Cobell
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------*/
import QtQuick.Window 2.11
import QtQuick 2.11
import QtQuick.Controls 1.4
import QtLocation 5.9
import QtPositioning 5.8

MapViewer {
    id: mapView
    Plugin {
        id: mapPlugin
        name: "mapboxgl"
        PluginParameter { name: "mapbox.access_token"; value: mapboxKey }
    }
}
