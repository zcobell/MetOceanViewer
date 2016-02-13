//-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2015  Zach Cobell
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
// The name "MetOcean Viewer" is specific to this project and may not be
// used for projects "forked" or derived from this work.
//
//-----------------------------------------------------------------------//
#include <hwm.h>

int hwm::readHWMData()
{
    QString Line;
    QStringList List;
    int nLines;

    QFile MyFile(this->fileBox->text());

    //Check if we can open the file
    if(!MyFile.open(QIODevice::ReadOnly|QIODevice::Text))
    {
        this->hwmErrorString = MyFile.errorString();
        return -1;
    }

    try
    {
        nLines = 0;
        while(!MyFile.atEnd())
        {
            Line = MyFile.readLine();
            nLines = nLines + 1;
        }
        MyFile.close();
        MyFile.open(QIODevice::ReadOnly|QIODevice::Text);
        this->highWaterMarks.resize(nLines);
        int i=0;
        while(!MyFile.atEnd())
        {
            Line = MyFile.readLine().simplified();
            List = Line.split(",");
            this->highWaterMarks[i].lon = List.value(0).toDouble();
            this->highWaterMarks[i].lat = List.value(1).toDouble();
            this->highWaterMarks[i].bathy = List.value(2).toDouble();
            this->highWaterMarks[i].measured = List.value(3).toDouble();
            this->highWaterMarks[i].modeled = List.value(4).toDouble();
            this->highWaterMarks[i].error = List.value(5).toDouble();
            i = i + 1;
        }
        return 0;
    }
    catch(...)
    {
        this->hwmErrorString = "Unexpected error reading file.";
        return 1;
    }

}
