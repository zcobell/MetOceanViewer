# MetOceanViewer
[![Build Status](https://travis-ci.org/zcobell/MetOceanViewer.svg?branch=master)](https://travis-ci.org/zcobell/MetOceanViewer)
[![Codacy Badge](https://api.codacy.com/project/badge/Grade/2d817318594440a0834476c9b06de219)](https://www.codacy.com/app/zachary.cobell/MetOceanViewer?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=zcobell/MetOceanViewer&amp;utm_campaign=Badge_Grade)

Multipurpose tool for viewing hydrodynamic model data, such as ADCIRC, with a gui interface. The code is written in Qt C++ and is designed to run on Windows, Mac OSX, and Linux systems.

# Feature List
- [x] Access and view NOAA station data
- [x] Access and view USGS station data
- [x] Create tide predictions using XTide
- [x] Read ADCIRC fort.61 files (when accompanied by station file)
- [x] Read ADCIRC netCDF formatted files
- [x] Read/write IMEDS formatted data
- [x] Read HWM comparisons, plot to map, and calculate statistics

# Screenshots
![Screenshot 1](https://github.com/zcobell/MetOceanViewer/blob/master/MetOceanViewer_GUI/screenshots/mov_ss1.JPG)
![Screenshot 2](https://github.com/zcobell/MetOceanViewer/blob/master/MetOceanViewer_GUI/screenshots/mov_ss2.JPG)
![Screenshot 3](https://github.com/zcobell/MetOceanViewer/blob/master/MetOceanViewer_GUI/screenshots/mov_ss3.JPG)

# Compiling
To compile this program, please use the Qt suite of tools. The MetOceanViewer.pro file has been tested using QtCreator and Qt version 5.10.1 on Windows, Mac OSX, and Linux. Prior versions of Qt should work, but have not been tested.

## netCDF
You will need to have netCDF version 4+ installed on your system with acccess to the headers and libraries. You may need to edit the netCDF path in MetOceanViewer.pro to correctly locate these files. When compiling for Windows, these libraries are already included in the package. However, Unix/Mac users will need to ensure they are in their LD_LIBRARY_PATH variable or configure the .pro file accordingly.

# Credits
The following external libraries/APIs are used in this software

|Package|Source|
|-------|------|
| Qt | http://www.qt.io |
| NOAA CO-OPS API | http://tidesandcurrents.noaa.gov|
| USGS Waterdata API | http://waterdata.usgs.gov|
| netCDF v4.3.3 | http://www.unidata.ucar.edu/software/netcdf)
| HDF5 v1.8.14 | http://www.hdfgroup.org/HDF5 |
| Curl v7.35.0 |http://curl.haxx.se |
| Proj.4 v4.9.2 | http://proj4.org |
| zlib v1.2.8 |http://www.zlib.net|
| OpenSSL v1.0.2d |https://www.openssl.org/|
| XTide |http://www.flaterco.com/xtide/|

# License
This program conforms to the GNU GPL, Version 3.
