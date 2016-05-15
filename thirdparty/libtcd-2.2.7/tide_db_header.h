/* $Id: tide_db_header.h 1092 2006-11-16 03:02:42Z flaterco $ */

/*****************************************************************************\

                            DISTRIBUTION STATEMENT

    This source file is unclassified, distribution unlimited, public
    domain.  It is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

\*****************************************************************************/




/***************************************************************************\

    Module Name:        TIDE DB option header file

    Programmer(s):      Jan C. Depner

    Date Written:       July 2002

    Purpose:            This header file defines all of the format options
                        that are stored in the TIDE DB file header.  To add a
                        new option you simply add it to the header_data
                        structure, then put a definition in the keys structure.
                        The keys structure contains a keyphrase such as
                        "[DATE]", a format like "%s", a datatype (see the
                        union in the TIDE_HEADER_DATA structure, a pointer to
                        the location at which to store the value(s), a count
                        if it is a repeating field (array), and a version
                        dependency flag (major.minor * 10).



*****************************************************************************

    This header file uses the NAVO standard data types.  You must set one of
    the following options on the compile command line:

    NVDOS, NVHPUX, NVIRIX, NVCLIX, NVLinux, NVSUNOS, NVWINNT, or NVWIN3X

The only one referenced in libtcd is NVWIN3X. -- DWF, 2004-09-29

\***************************************************************************/


#ifndef __TIDE_HEADER__
#define __TIDE_HEADER__

#include "tcd.h"


typedef struct
{
    NV_CHAR                    *keyphrase;
    NV_CHAR                    *datatype;
    union
    {
        NV_CHAR                *cstr;
        NV_INT32               *i32;
        NV_U_INT32             *ui32;
    } address;
} KEY;


typedef struct
{
    DB_HEADER_PUBLIC  pub;
    NV_CHAR           **constituent;
    NV_FLOAT64        *speed;
    NV_FLOAT32        **equilibrium;
    NV_FLOAT32        **node_factor;
    NV_CHAR           **level_unit;
    NV_CHAR           **dir_unit;
    NV_CHAR           **restriction;
    NV_CHAR           **tzfile;
    NV_CHAR           **country;
    NV_CHAR           **datum;
    NV_CHAR           **legalese;
    NV_U_INT32        header_size;
    NV_U_INT32        speed_bits;
    NV_U_INT32        speed_scale;
    NV_INT32          speed_offset;
    NV_U_INT32        equilibrium_bits;
    NV_U_INT32        equilibrium_scale;
    NV_INT32          equilibrium_offset;
    NV_U_INT32        node_bits;
    NV_U_INT32        node_scale;
    NV_INT32          node_offset;
    NV_U_INT32        amplitude_bits;
    NV_U_INT32        amplitude_scale;
    NV_U_INT32        epoch_bits;
    NV_U_INT32        epoch_scale;
    NV_U_INT32        constituent_bits;
    NV_U_INT32        record_type_bits;
    NV_U_INT32        latitude_scale;
    NV_U_INT32        latitude_bits;
    NV_U_INT32        longitude_scale;
    NV_U_INT32        longitude_bits;
    NV_U_INT32        record_size_bits;
    NV_U_INT32        station_bits;
    NV_U_INT32        level_unit_bits;
    NV_U_INT32        dir_unit_bits;
    NV_U_INT32        restriction_bits;
    NV_U_INT32        max_restriction_types;
    NV_U_INT32        tzfile_bits;
    NV_U_INT32        max_tzfiles;
    NV_U_INT32        country_bits;
    NV_U_INT32        max_countries;
    NV_U_INT32        datum_bits;
    NV_U_INT32        max_datum_types;
    NV_U_INT32        legalese_bits;
    NV_U_INT32        max_legaleses;
    NV_U_INT32        datum_offset_bits;
    NV_U_INT32        datum_offset_scale;
    NV_U_INT32        date_bits;
    NV_U_INT32        months_on_station_bits;
    NV_U_INT32        confidence_value_bits;
    NV_U_INT32        time_bits;
    NV_U_INT32        level_add_bits;
    NV_U_INT32        level_add_scale;
    NV_U_INT32        level_multiply_bits;
    NV_U_INT32        level_multiply_scale;
    NV_U_INT32        direction_bits;
    NV_U_INT32        constituent_size;
    NV_U_INT32        level_unit_size;
    NV_U_INT32        dir_unit_size;
    NV_U_INT32        restriction_size;
    NV_U_INT32        tzfile_size;
    NV_U_INT32        country_size;
    NV_U_INT32        datum_size;
    NV_U_INT32        legalese_size;
    NV_U_INT32        end_of_file;

    /* Need these to read V1 files. */
    NV_U_INT32        pedigree_bits;
    NV_U_INT32        pedigree_size;
} TIDE_HEADER_DATA;


static TIDE_HEADER_DATA        hd;

/* "The initializer for a union with no constructor is either a single
   expression of the same type, or a brace-enclosed initializer for
   the first member of the union." -- Ellis & Stroustrup, The Annotated
   C++ Reference Manual, 1990, Section 8.4.1. */

static KEY                     keys[] =
{
    {
        "[VERSION]",
        "cstr",
        {(NV_CHAR *) hd.pub.version},
    },
    {
        "[MAJOR REV]",
        "ui32",
        {(NV_CHAR *) &hd.pub.major_rev},
    },
    {
        "[MINOR REV]",
        "ui32",
        {(NV_CHAR *) &hd.pub.minor_rev},
    },
    {
        "[LAST MODIFIED]",
        "cstr",
        {(NV_CHAR *) hd.pub.last_modified},
    },
    {
        "[NUMBER OF RECORDS]",
        "ui32",
        {(NV_CHAR *) &hd.pub.number_of_records},
    },
    {
        "[START YEAR]",
        "i32",
        {(NV_CHAR *) &hd.pub.start_year},
    },
    {
        "[NUMBER OF YEARS]",
        "ui32",
        {(NV_CHAR *) &hd.pub.number_of_years},
    },
    {
        "[CONSTITUENTS]",
        "ui32",
        {(NV_CHAR *) &hd.pub.constituents},
    },
    {
        "[LEVEL UNIT TYPES]",
        "ui32",
        {(NV_CHAR *) &hd.pub.level_unit_types},
    },
    {
        "[DIRECTION UNIT TYPES]",
        "ui32",
        {(NV_CHAR *) &hd.pub.dir_unit_types},
    },
    {
        "[RESTRICTION TYPES]",
        "ui32",
        {(NV_CHAR *) &hd.pub.restriction_types},
    },
    {
        "[PEDIGREE TYPES]",
        "ui32",
        {(NV_CHAR *) &hd.pub.pedigree_types},
    },
    {
        "[TZFILES]",
        "ui32",
        {(NV_CHAR *) &hd.pub.tzfiles},
    },
    {
        "[COUNTRIES]",
        "ui32",
        {(NV_CHAR *) &hd.pub.countries},
    },
    {
        "[DATUM TYPES]",
        "ui32",
        {(NV_CHAR *) &hd.pub.datum_types},
    },
    {
        "[LEGALESES]",
        "ui32",
        {(NV_CHAR *) &hd.pub.legaleses},
    },
    {
        "[HEADER SIZE]",
        "ui32",
        {(NV_CHAR *) &hd.header_size},
    },
    {
        "[SPEED BITS]",
        "ui32",
        {(NV_CHAR *) &hd.speed_bits},
    },
    {
        "[SPEED SCALE]",
        "ui32",
        {(NV_CHAR *) &hd.speed_scale},
    },
    {
        "[SPEED OFFSET]",
        "i32",
        {(NV_CHAR *) &hd.speed_offset},
    },
    {
        "[EQUILIBRIUM BITS]",
        "ui32",
        {(NV_CHAR *) &hd.equilibrium_bits},
    },
    {
        "[EQUILIBRIUM SCALE]",
        "ui32",
        {(NV_CHAR *) &hd.equilibrium_scale},
    },
    {
        "[EQUILIBRIUM OFFSET]",
        "i32",
        {(NV_CHAR *) &hd.equilibrium_offset},
    },
    {
        "[NODE BITS]",
        "ui32",
        {(NV_CHAR *) &hd.node_bits},
    },
    {
        "[NODE SCALE]",
        "ui32",
        {(NV_CHAR *) &hd.node_scale},
    },
    {
        "[NODE OFFSET]",
        "i32",
        {(NV_CHAR *) &hd.node_offset},
    },
    {
        "[AMPLITUDE BITS]",
        "ui32",
        {(NV_CHAR *) &hd.amplitude_bits},
    },
    {
        "[AMPLITUDE SCALE]",
        "ui32",
        {(NV_CHAR *) &hd.amplitude_scale},
    },
    {
        "[EPOCH BITS]",
        "ui32",
        {(NV_CHAR *) &hd.epoch_bits},
    },
    {
        "[EPOCH SCALE]",
        "ui32",
        {(NV_CHAR *) &hd.epoch_scale},
    },
    {
        "[CONSTITUENT BITS]",
        "ui32",
        {(NV_CHAR *) &hd.constituent_bits},
    },
    {
        "[LEVEL UNIT BITS]",
        "ui32",
        {(NV_CHAR *) &hd.level_unit_bits},
    },
    {
        "[DIRECTION UNIT BITS]",
        "ui32",
        {(NV_CHAR *) &hd.dir_unit_bits},
    },
    {
        "[RESTRICTION BITS]",
        "ui32",
        {(NV_CHAR *) &hd.restriction_bits},
    },
    {
        "[PEDIGREE BITS]",
        "ui32",
        {(NV_CHAR *) &hd.pedigree_bits},
    },
    {
        "[TZFILE BITS]",
        "ui32",
        {(NV_CHAR *) &hd.tzfile_bits},
    },
    {
        "[COUNTRY BITS]",
        "ui32",
        {(NV_CHAR *) &hd.country_bits},
    },
    {
        "[DATUM BITS]",
        "ui32",
        {(NV_CHAR *) &hd.datum_bits},
    },
    {
        "[LEGALESE BITS]",
        "ui32",
        {(NV_CHAR *) &hd.legalese_bits},
    },
    {
        "[RECORD TYPE BITS]",
        "ui32",
        {(NV_CHAR *) &hd.record_type_bits},
    },
    {
        "[LATITUDE SCALE]",
        "ui32",
        {(NV_CHAR *) &hd.latitude_scale},
    },
    {
        "[LATITUDE BITS]",
        "ui32",
        {(NV_CHAR *) &hd.latitude_bits},
    },
    {
        "[LONGITUDE SCALE]",
        "ui32",
        {(NV_CHAR *) &hd.longitude_scale},
    },
    {
        "[LONGITUDE BITS]",
        "ui32",
        {(NV_CHAR *) &hd.longitude_bits},
    },
    {
        "[RECORD SIZE BITS]",
        "ui32",
        {(NV_CHAR *) &hd.record_size_bits},
    },
    {
        "[STATION BITS]",
        "ui32",
        {(NV_CHAR *) &hd.station_bits},
    },
    {
        "[DATUM OFFSET BITS]",
        "ui32",
        {(NV_CHAR *) &hd.datum_offset_bits},
    },
    {
        "[DATUM OFFSET SCALE]",
        "ui32",
        {(NV_CHAR *) &hd.datum_offset_scale},
    },
    {
        "[DATE BITS]",
        "ui32",
        {(NV_CHAR *) &hd.date_bits},
    },
    {
        "[MONTHS ON STATION BITS]",
        "ui32",
        {(NV_CHAR *) &hd.months_on_station_bits},
    },
    {
        "[CONFIDENCE VALUE BITS]",
        "ui32",
        {(NV_CHAR *) &hd.confidence_value_bits},
    },
    {
        "[TIME BITS]",
        "ui32",
        {(NV_CHAR *) &hd.time_bits},
    },
    {
        "[LEVEL ADD BITS]",
        "ui32",
        {(NV_CHAR *) &hd.level_add_bits},
    },
    {
        "[LEVEL ADD SCALE]",
        "ui32",
        {(NV_CHAR *) &hd.level_add_scale},
    },
    {
        "[LEVEL MULTIPLY BITS]",
        "ui32",
        {(NV_CHAR *) &hd.level_multiply_bits},
    },
    {
        "[LEVEL MULTIPLY SCALE]",
        "ui32",
        {(NV_CHAR *) &hd.level_multiply_scale},
    },
    {
        "[DIRECTION BITS]",
        "ui32",
        {(NV_CHAR *) &hd.direction_bits},
    },
    {
        "[CONSTITUENT SIZE]",
        "ui32",
        {(NV_CHAR *) &hd.constituent_size},
    },
    {
        "[LEVEL UNIT SIZE]",
        "ui32",
        {(NV_CHAR *) &hd.level_unit_size},
    },
    {
        "[DIRECTION UNIT SIZE]",
        "ui32",
        {(NV_CHAR *) &hd.dir_unit_size},
    },
    {
        "[RESTRICTION SIZE]",
        "ui32",
        {(NV_CHAR *) &hd.restriction_size},
    },
    {
        "[PEDIGREE SIZE]",
        "ui32",
        {(NV_CHAR *) &hd.pedigree_size},
    },
    {
        "[TZFILE SIZE]",
        "ui32",
        {(NV_CHAR *) &hd.tzfile_size},
    },
    {
        "[COUNTRY SIZE]",
        "ui32",
        {(NV_CHAR *) &hd.country_size},
    },
    {
        "[DATUM SIZE]",
        "ui32",
        {(NV_CHAR *) &hd.datum_size},
    },
    {
        "[LEGALESE SIZE]",
        "ui32",
        {(NV_CHAR *) &hd.legalese_size},
    },
    {
        "[END OF FILE]",
        "ui32",
        {(NV_CHAR *) &hd.end_of_file},
    }
};
#endif
