/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2019  Zach Cobell
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
#ifndef OPTIONSLIST_H
#define OPTIONSLIST_H

#include <QCommandLineOption>

static const QCommandLineOption m_serviceType =
    QCommandLineOption(QStringList() << "s"
                                     << "service",
                       "Service to use to generate data. Can be one of NOAA, "
                       "USGS, NDBC, or XTIDE",
                       "source");
static const QCommandLineOption m_stationId = QCommandLineOption(
    QStringList() << "station",
    "Station ID to use to generate data. Ex. NOAA Station 2695540 would "
    "be be specified with '2695540'. XTide stations should be specified "
    "using their ID from MetOceanViewer",
    "id");

static const QCommandLineOption m_startDate =
    QCommandLineOption(QStringList() << "b"
                                     << "startdate",
                       "Start date for generating the data. Should be "
                       "formatted as: yyyyMMddhhmmss",
                       "date");

static const QCommandLineOption m_endDate = QCommandLineOption(
    QStringList() << "e"
                  << "enddate",
    "End date for generating the data. Should be formatted as yyyyMMddhhmmss",
    "date");

static const QCommandLineOption m_product =
    QCommandLineOption(QStringList() << "p"
                                     << "product",
                       "Product index to download numbered from 1 to the "
                       "number of available products. If left unspecified, "
                       "you will be presented with a list of options",
                       "index");

static const QCommandLineOption m_datum = QCommandLineOption(
    QStringList() << "d"
                  << "datum",
    "Specified datum to use. Only available for NOAA and XTide products",
    "option");

static const QCommandLineOption m_outputFile =
    QCommandLineOption(QStringList() << "o"
                                     << "output",
                       "Name of the output file. Format will be guessed from "
                       "extension (.imeds or .nc). Note if performing crms "
                       "processing, the output extension will always be *.nc",
                       "filename");

static const QCommandLineOption m_boundingBox =
    QCommandLineOption(QStringList() << "boundingbox",
                       "Bounding box coordinates. Selects all stations that "
                       "fall within the bounding box",
                       "x1,y1,x2,y2");

static const QCommandLineOption m_nearest = QCommandLineOption(
    QStringList() << "nearest",
    "Selects the station that falls closest to the specfied location", "x,y");

static const QCommandLineOption m_list =
    QCommandLineOption(QStringList() << "list",
                       "Provide a list of stations via input file formatted "
                       "using a list of station id numbers",
                       "file");

static const QCommandLineOption m_show =
    QCommandLineOption(QStringList() << "show",
                       "Show the stations that would be selected given the "
                       "provided criteria and exit");

static const QCommandLineOption m_vdatum =
    QCommandLineOption(QStringList() << "vdatum",
                       "Use NOAA VDatum transformations where available");

static const QCommandLineOption m_parameterId = QCommandLineOption(
    QStringList() << "parameter", "Parameter codes for USGS", "code");

#endif  // OPTIONSLIST_H
