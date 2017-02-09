//---- LGPL --------------------------------------------------------------------
//
// Copyright (C)  Stichting Deltares, 2011-2016.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation version 2.1.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, see <http://www.gnu.org/licenses/>.
//
// contact: delft3d.support@deltares.nl
// Stichting Deltares
// P.O. Box 177
// 2600 MH Delft, The Netherlands
//
// All indications and logos of, and references to, "Delft3D" and "Deltares"
// are registered trademarks of Stichting Deltares, and remain the property of
// Stichting Deltares. All rights reserved.
//
//------------------------------------------------------------------------------
// $Id: nefis_version.cpp 5838 2016-02-12 12:21:38Z mourits $
// $HeadURL: https://svn.oss.deltares.nl/repos/delft3d/tags/6118/src/utils_lgpl/nefis/packages/nefis/src/nefis_version.cpp $
//#include <stdio.h>
#include "nefis_version.h"

#if defined(W32)
static char nefis_version [] = {nefis_major "." nefis_minor "." nefis_revision "." nefis_build " (Win32)"};
static char nefis_version_id [] = {"@(#)Deltares, " nefis_program " Version " nefis_major "." nefis_minor "." nefis_revision "." nefis_build " (Win32), " __DATE__ ", " __TIME__ ""};
#elif defined(W64)
static char nefis_version [] = {nefis_major "." nefis_minor "." nefis_revision "." nefis_build " (Win64)"};
static char nefis_version_id [] = {"@(#)Deltares, " nefis_program " Version " nefis_major "." nefis_minor "." nefis_revision "." nefis_build " (Win64), " __DATE__ ", " __TIME__ ""};
#elif defined(LINUX64)
static char nefis_version [] = {nefis_major "." nefis_minor "." nefis_revision "." nefis_build " (Win64)"};
static char nefis_version_id [] = {"@(#)Deltares, " nefis_program " Version " nefis_major "." nefis_minor "." nefis_revision "." nefis_build " (Linux64), " __DATE__ ", " __TIME__ ""};
#else
static char nefis_version [] = {nefis_major "." nefis_minor "." nefis_revision "." nefis_build " (Unknown)"};
static char nefis_version_id [] = {"@(#)Deltares, " nefis_program " Version " nefis_major "." nefis_minor "." nefis_revision "." nefis_build " (Unknown), " __DATE__ ", " __TIME__ ""};
#endif
static char nefis_file_version [] = {"5.00.00"};
static char nefis_company_name [] = {"Deltares"};

extern "C" {
    char * getfullversionstring_nefis(void)
    {
        return nefis_version_id;
    };
    char * getfileversionstring_nefis(void)
    {
        return nefis_file_version;
    };
    char * getcompanystring_nefis(void)
    {
        return nefis_company_name;
    };
}
