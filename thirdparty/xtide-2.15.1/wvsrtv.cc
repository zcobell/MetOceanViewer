// $Id: wvsrtv.cc 5749 2014-10-11 19:42:10Z flaterco $

/*  wvsrtv  Retrieve WVS data from compressed files.


                            DISTRIBUTION STATEMENT

    This source file is unclassified, distribution unlimited, public
    domain.  It is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.



        The following functions are used to access the compressed World
    Vector Shoreline (WVS) files as they were stored on the National
    Geophysical Data Center (NGDC) Global Relief Data CD-ROM.  These files,
    and the compression method used on them, were designed and built by
    myself and James Hammack (then of NORDA).  The work was done in 1989.
    At that time the 350MB that was required to store the entire WVS data
    set was considered too large to deal with easily.  In addition, the data
    was sequential access, ASCII format which required that you read the
    entire data set to retrieve all of the data for any subset.  The data is
    stored in a direct access data file as unsigned character data.  There
    are no endian or floating point format issues with this data set.  It is
    organized by one-degree cells so that any one-degree cell is accessible
    with two reads.  The compression used is a simple delta coding method.
    The full resolution data set was compressed from 350MB to 25MB.  In
    addition to the full resolution data set, which was slightly better than
    1:250,000 scale, Jerry Landrum (NORDA) used the Douglas-Peuker algorithm
    to sub-sample the data to lower resolution data sets.  The resulting
    files were then compressed using our method to produce the following
    files:

        wvsfull.dat    -    full resolution     -    25,313,280 bytes
        wvs250k.dat    -    1:250,000           -    18,232,320 bytes
        wvs1.dat       -    1:1,000,000         -    4,896,768 bytes
        wvs3.dat       -    1:3,000,000         -    5,121,024 bytes
        wvs12.dat      -    1:12,000,000        -    2,933,760 bytes
        wvs43.dat      -    1:43,000,000        -    2,233,344 bytes

    Notice that the 1:3,000,000 scale data set is actually larger than the
    1:1,000,000 scale data set.  Go figure.  There isn't much use in using
    the 1:3,000,000 scale data set unless you just want less detailed
    shorelines.  The NGDC Global Relief Data CD-ROM also contains the CIA
    World Data Bank II coast, rivers, and political boundaries data files.
    These functions will not work with those files.  The coastlines were
    only 1:1,000,000 scale and the rivers and political boundaries don't
    match with the WVS data.  These functions can be modified to work with
    that data by including segment breaking based on data rank but it hardly
    seems worth the effort (which is why I didn't do it).

        For the masochistic I recommend reading A Portable Method For
    Compressing and Storing High-Resolution Geographic Data Sets, Jan C.
    Depner and James A. Hammack, Naval Oceanographic Office, 1989.  It
    describes the compression, storage, and retrieving methods in painful
    detail.  Every time I read it I get confused all over again.

        The following functions allow the user to retrieve the data from
    the data files one degree at a time.  This is the way I should have done
    it in the first place.  You must define an environment variable (WVS_DIR)
    that points to the directory containing the coastline files.  The
    coastline files must use the names defined above (as they were stored on
    the NGDC CD-ROM).  The functions will "fall through" to the next level
    data file if the requested one is not available.  That is, if you request
    full resolution and all you have is 1:12,000,000 it will switch to the
    lower resolution file.  After the last call to wvsrtv you should call it
    one more time with "clean" as the file name.  This will cause it to free
    memory and close any open files.

        To paraphrase my heroes M. Horwitz, J. Horwitz, and L. Fineberg,
    a-plotting we will go!


    Jan C. Depner
    Naval Oceanographic Office
    depnerj@navo.navy.mil
    February 17, 2002

*/



#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <assert.h>


#define         PHYSIZ      3072

#define         SIGN_OF(x)  ((x)<0.0 ? -1 : 1)


//RWL make paths either WIN or Unix

#if defined(WIN32) || defined(_MSC_VER)
#define DIR_PATH_CHAR "\\"
#else
#define DIR_PATH_CHAR "/"
#endif


/***************************************************************************\
*       Function nxtrec                                                     *
*****************************************************************************
*                                                                           *
*       Reads next record in overflow chain.                                *
*                                                                           *
*       Author :                                                            *
*                                                                           *
*         Jan C. Depner                                                     *
*         Naval Oceanographic Office                                        *
*         Oceanographic Support Staff (Code N4O)                            *
*         Hydrographic Department                                           *
*         Stennis Space Center, MS                                          *
*         39522-5001                                                        *
*         depnerj@navo.navy.mil                                             *
*                                                                           *
*                                                                           *
*       Arguments:                                                          *
*                                                                           *
*       rindex    - cell index number for algorithm addressing, rindex =    *
*                   integer (lat degrees + 90) * 360 + integer lon          *
*                   degrees + 180 + 1 ; this is the logical record          *
*                   address of the first data segment in any cell.          *
*       bytbuf    - char array of 'PHYSIZ' length for i/o to the direct     *
*                   access file.                                            *
*       curpos    - current byte position within the physical record.       *
*       addr      - current physical record address in bytes from beginning *
*                   of the file.                                            *
*       lperp     - logical records per physical record.                    *
*       paddr     - previous physical record address.                       *
*       fp        - file pointer for the direct access file.                *
*       logrec    - length of a logical record in bytes.                    *
*                                                                           *
\***************************************************************************/

static void nxtrec (long *rindex, unsigned char *bytbuf, long *curpos,
                    long *addr, int lperp, long *paddr, FILE *fp, int logrec)
{
    /*  Compute the index number for the next logical record in the chain.   */

    *rindex = bytbuf[*curpos + 1] * (long) 65536 + bytbuf[*curpos + 2] *
        (long) 256 + bytbuf[*curpos + 3];
    *addr = ((*rindex - 1) / lperp) * PHYSIZ;


    /*  If the physical record has changed since the last access, read a
        new physical record.  */

    if (*addr != *paddr)
    {
      // DWF added error checking
      if (fseek (fp, *addr, 0)) {
        fprintf (stderr, "nxtrec:  error on fseek\n");
        perror ("nxtrec");
        exit (-1);
      }
      if (fread (bytbuf, PHYSIZ, 1, fp) < 1) {
        fprintf (stderr, "nxtrec:  error on fread\n");
        exit (-1);
      }
    }


    /*  Set the previous physical address to the current one, and compute
        the current byte position for the new record.  */

    *paddr = *addr;
    *curpos = ((*rindex - 1) % lperp) * logrec;
}



/***************************************************************************\
*       Function movpos                                                     *
*****************************************************************************
*                                                                           *
*       Updates current position pointer and checks for end of record.      *
*                                                                           *
*       Author :                                                            *
*                                                                           *
*         Jan C. Depner                                                     *
*         Naval Oceanographic Office                                        *
*         Oceanographic Support Staff (Code N4O)                            *
*         Hydrographic Department                                           *
*         Stennis Space Center, MS                                          *
*         39522-5001                                                        *
*         depnerj@navo.navy.mil                                             *
*                                                                           *
*                                                                           *
*       Arguments:                                                          *
*                                                                           *
*       rindex    - cell index number for algorithm addressing, rindex =    *
*                   integer (lat degrees + 90) * 360 + integer lon          *
*                   degrees + 180 + 1 ; this is the logical record          *
*                   address of the first data segment in any cell.          *
*       bytbuf    - char array of 'PHYSIZ' length for i/o to the direct     *
*                   access file.                                            *
*       curpos    - current byte position within the physical record.       *
*       addr      - current physical record address in bytes from beginning *
*                   of the file.                                            *
*       lperp     - logical records per physical record.                    *
*       paddr     - previous physical record address.                       *
*       fp        - file pointer for the direct access file.                *
*       logrec    - length of a logical record in bytes.                    *
*       fulrec    - full record value (logrec-4).                           *
*                                                                           *
\***************************************************************************/

static void movpos (long *rindex, unsigned char *bytbuf, long *curpos,
                    long *addr, int lperp, long *paddr, FILE *fp, int logrec,
                    long fulrec)
{
    *curpos = *curpos + 2;


    /*  If we hit the end of the physical record, get the next one.  */

    if (*curpos % logrec == fulrec) nxtrec (rindex, bytbuf, curpos, addr,
        lperp, paddr, fp, logrec);
}



/***************************************************************************\
*       Function test_bit                                                   *
*****************************************************************************
*                                                                           *
*       Checks for bit set in an unsigned char.                             *
*                                                                           *
*       Author :                                                            *
*                                                                           *
*         Jan C. Depner                                                     *
*         Naval Oceanographic Office                                        *
*         Oceanographic Support Staff (Code N4O)                            *
*         Hydrographic Department                                           *
*         Stennis Space Center, MS                                          *
*         39522-5001                                                        *
*         depnerj@navo.navy.mil                                             *
*                                                                           *
*                                                                           *
*       Variable definitions :                                              *
*                                                                           *
*       mask  - char array of bit masks                                     *
*                                                                           *
*       Arguments:                                                          *
*                                                                           *
*       byte      - should be self-explanatory                              *
*       bitpos    - bit position within byte                                *
*                                                                           *
\***************************************************************************/

static int test_bit (unsigned char byte, int bitpos)
{
    static unsigned char    mask[9] = {0x1, 0x2, 0x4, 0x8, 0x10, 0x20,
                            0x40, 0x80};

    return (byte & mask[bitpos]);
}



/***************************************************************************\
*       Function celchk                                                     *
*****************************************************************************
*                                                                           *
*       Checks for data in a given one-degree cell, reads bit map.          *
*                                                                           *
*       Author :                                                            *
*                                                                           *
*         Jan C. Depner                                                     *
*         Naval Oceanographic Office                                        *
*         Oceanographic Support Staff (Code N4O)                            *
*         Hydrographic Department                                           *
*         Stennis Space Center, MS                                          *
*         39522-5001                                                        *
*         depnerj@navo.navy.mil                                             *
*                                                                           *
*                                                                           *
*       Variable definitions :                                              *
*                                                                           *
*       caddr  - current physical record address in bytes from beginning    *
*                of the file (cell map address).                            *
*       ndxpos - bit position within the 64800 bit cell map for the cell    *
*                pointed to by 'rindex'.                                    *
*       bytpos - byte position within the cell map of the 'rindex' cell.    *
*       bitpos - bit position within the 'bytpos' byte of the 'rindex' cell *
*                bit.                                                       *
*       chk    - logical value returned (true if there is data in the       *
*                'rindex' cell).                                            *
*                                                                           *
*       Arguments:                                                          *
*                                                                           *
*       rindex    - cell index number for algorithm addressing, rindex =    *
*                   integer (lat degrees + 90) * 360 + integer lon          *
*                   degrees + 180 + 1 ; this is the logical record          *
*                   address of the first data segment in any cell.          *
*       logrec    - length of a logical record in bytes.                    *
*       fp        - file pointer for the direct access file.                *
*       pcaddr    - previous physical record address (cell map address).    *
*                                                                           *
\***************************************************************************/

static int celchk (int rindex, int logrec, int offset, FILE *fp, long *pcaddr)
{
    static unsigned char    celbuf[PHYSIZ];
    static long             caddr;
    long                    ndxpos;
    int                     bytpos, bitpos, chk;


    /*  Compute the physical address of the 'rindex' cell bit. */

    caddr = (((rindex + logrec * 8) - (offset + 1)) /
        (PHYSIZ * 8)) * PHYSIZ;


    /*  If this is the first access or the physical address has changed
        since the last access, read a new physical record.  */

    if (*pcaddr != caddr)
    {
      // DWF added error checking
      if (fseek (fp, caddr, 0)) {
        fprintf (stderr, "celchk:  error on fseek\n");
        perror ("celchk");
        exit (-1);
      }
      if (fread (celbuf, PHYSIZ, 1, fp) < 1) {
        fprintf (stderr, "celchk:  error on fread\n");
        exit (-1);
      }
    }


    /*  Set the previous address to the current one.  */

    *pcaddr = caddr;


    /*  Compute the 'rindex' position within the physical record.  */

    ndxpos = ((rindex + logrec * 8) - (offset + 1)) % (PHYSIZ * 8);


    /*  Compute the byte and bit positions.  */

    bytpos = ndxpos / 8;
    bitpos = 7 - ndxpos % 8;


    /*  Test the 'rindex' bit and return.  */

    chk = test_bit (celbuf[bytpos], bitpos);
    return (chk);
}




/***************************************************************************\
*       Function build_seg                                                  *
*****************************************************************************
*                                                                           *
*       Build the arrays containing the segments.                           *
*                                                                           *
*       Author :                                                            *
*                                                                           *
*         Jan C. Depner                                                     *
*         Naval Oceanographic Office                                        *
*         Oceanographic Support Staff (Code N4O)                            *
*         Hydrographic Department                                           *
*         Stennis Space Center, MS                                          *
*         39522-5001                                                        *
*         depnerj@navo.navy.mil                                             *
*                                                                           *
*                                                                           *
*       Arguments:                                                          *
*                                                                           *
*       dlat   - latitude of current point (degrees)                        *
*       dlon   - longitude of current point (degrees)                       *
*       cont   - segment continuation flag.                                 *
*       nsegs  - number of segments in this cell                            *
*       npts   - number of points in this segment                           *
*       total  - total points in this cell                                  *
*       latray - latitude array pointer                                     *
*       lonray - longitude array pointer                                    *
*       segray - segment count array pointer                                *
*       lnbias - longitude offset (if in 0-360 world)                       *
*                                                                           *
****************************************************************************/

static void build_seg (float dlat, float dlon, int *cont, int *nsegs,
                       int *npts, int *total, float **latray, float **lonray,
                       int **segray, int lnbias)
{
    /*  If this is beginning of a new segment, close the last segment.  */

    if (!*cont)
    {
        /*  Make sure there are at least two points in the arrays.  */

        if (*npts > 0)
        {
            if ((*segray = (int *) realloc (*segray, (*nsegs + 1) *
                sizeof (int))) == NULL)
            {
                perror (__FILE__);
                exit (-1);
            }

            (*segray)[*nsegs] = *npts + 1;
            ++(*nsegs);
        }
        else
        {
            /*  Back up a spot if we got a single point segment.  */

            if (*total >= 0) --(*total);
        }

        *npts = -1;
    }


    /*  Reallocate memory and store point.  */

    ++(*npts);
    ++(*total);

    if ((*latray = (float *) realloc (*latray, ((*total) + 1) *
        sizeof (float))) == NULL || (*lonray = (float *) realloc (*lonray,
        ((*total) + 1) * sizeof (float))) == NULL)
    {
        perror (__FILE__);
        exit (-1);
    }

    (*latray)[*total] = dlat - 90.0;
    (*lonray)[*total] = dlon - 180.0 + lnbias;
    *cont = -1;

    return;
}



/***************************************************************************\
*      Function wvsrtv                                                      *
*****************************************************************************
*      Retrieves the WVS data from the direct access files.                 *
*                                                                           *
*                                                                           *
*                                                                           *
*       Author :                                                            *
*                                                                           *
*         Jan C. Depner                                                     *
*         Naval Oceanographic Office                                        *
*         Oceanographic Support Staff (Code N4O)                            *
*         Hydrographic Department                                           *
*         Stennis Space Center, MS                                          *
*         39522-5001                                                        *
*         depnerj@navo.navy.mil                                             *
*                                                                           *
*                                                                           *
*       Variable definitions :                                              *
*                                                                           *
*       PHYSIZ    - size of a physical record in the direct access file,    *
*                   in bytes.                                               *
*       bytbuf    - char array of 'PHYSIZ' length for i/o to the direct     *
*                   access file.                                            *
*       celbuf    - char array of 'PHYSIZ' length for i/o to the            *
*                   direct access file (used to retrieve cell map).         *
*       fp        - file pointer for the direct access file.                *
*       logrec    - length of a logical record in bytes.                    *
*       lperp     - logical records per physical record.                    *
*       offset    - number of logical records needed to store the bit       *
*                   cell map + 1 .                                          *
*       version   - wvsrtv software and data file version.                  *
*       rindex    - cell index number for algorithm addressing, rindex =    *
*                   integer (lat degrees + 90) * 360 + integer lon          *
*                   degrees + 180 + 1 ; this is the logical record          *
*                   address of the first data segment in any cell.          *
*       addr      - current physical record address in bytes from beginning *
*                   of the file.                                            *
*       paddr     - previous physical record address.                       *
*       pcaddr    - previous physical record address (cell map address).    *
*       stat      - seek and read status variable.                          *
*       curpos    - current byte position within the physical record.       *
*       fulrec    - full record value (logrec-4).                           *
*       latdd     - latitude of one-degree cell (degrees, SW corner).       *
*       londd     - longitude of one-degree cell (degrees, SW corner).      *
*       ioff      - divisor for delta records (1 - WDB, 10 - WVS).          *
*       i         - utility variable.                                       *
*       j         - utility variable.                                       *
*       col       - longitude index.                                        *
*       lat       - integer value of lat of one-degree cell (SW corner).    *
*       lon       - integer value of lon of one-degree cell (SW corner).    *
*       slatf     - integer value of southern latitude of file (degrees).   *
*       nlatf     - integer value of northern latitude of file (degrees).   *
*       wlonf     - integer value of western longitude of file (degrees).   *
*       elonf     - integer value of eastern longitude of file (degrees).   *
*       widef     - integer width of file in degrees.                       *
*       size      - number of cells in the input file (for subset file).    *
*       segcnt    - number of data points in the current segment.           *
*       cont      - segment continuation flag.                              *
*       cnt       - segment loop counter.                                   *
*       latsec    - delta latitude in seconds or tenths of seconds.         *
*       lonsec    - delta longitude in seconds or tenths of seconds.        *
*       latoff    - latitude offset (seconds or tenths of seconds).         *
*       lonoff    - longitude offset (seconds or tenths of seconds).        *
*       conbyt    - byte position of continuation pointer within physical   *
*                   record.                                                 *
*       eflag     - end of segment flag.                                    *
*       todeg     - conversion factor from deltas to degrees.               *
*       dummy     - dummy pointer for modf function.                        *
*                                                                           *
*       Arguments:                                                          *
*                                                                           *
*       file   - NGDC Global Relief Data file name (wvsfull.dat, etc) or    *
*                "clean" to clear up memory, close files, etc.              *
*       dir    - directory containing WVS files (WVS_DIR)                   *
*       latd   - integer latitude degrees of one-degree cell (SW corner).   *
*       lond   - integer longitude degrees of one-degree cell (SW corner).  *
*       latray - array containing latitude points (returned)                *
*       lonray - array containing longitude points (returned)               *
*       segray - array containing counts for each segment (returned)        *
*                                                                           *
*       int    - number of segments, 0 on end, error, or no data            *
*                                                                           *
\***************************************************************************/

int wvsrtv (char const * const file,
            char const * const dir,
            int latd,
            int lond,
            float **latray,
            float **lonray,
            int **segray) {
    static FILE             *fp = NULL;
    static char             prev_file[60], files[6][12] =
                            {"wvsfull.dat", "wvs250k.dat", "wvs1.dat",
                            "wvs3.dat", "wvs12.dat", "wvs43.dat"};
    static int              logrec, lperp, offset, /*version,*/ slatf, nlatf,
                            wlonf, elonf, widef, ioff, npts;
    static bool             first = true;
    static long             rindex, addr, paddr, curpos, fulrec, size, pcaddr;
    static float            todeg;

    unsigned char           bytbuf[PHYSIZ];
    int                     i, j, col, segcnt, lat, lon, nsegs, total,
                            cont, cnt, latsec, lonsec, lnbias;
    bool                    eflag;
    long                    latoff, lonoff, conbyt, lats, lons;
    char                    dirfil[512], tmpfil[512];
    float                   dlat, dlon;



    /*  On the first call, set the arrays that will be malloc'ed to NULL.
        This is so that the user doesn't have to remember to do it (I
        always forget).  They will be freed and reset on subsequent
        "clean" calls.  */

    if (first)
    {
        *latray = (float *) NULL;
        *lonray = (float *) NULL;
        *segray = (int *) NULL;

        first = false;
    }


    /*  If the word "clean" is passed in as the file name, clean up memory
        and close the open file.  */

    if (!strcmp (file, "clean"))
    {
        if (*latray) free (*latray);
        if (*lonray) free (*lonray);
        if (*segray) free (*segray);

        *latray = (float *) NULL;
        *lonray = (float *) NULL;
        *segray = (int *) NULL;

        if (fp) fclose (fp);
        fp = (FILE *) NULL;

        strcpy (prev_file, file);

        return (0);
    }


    /*  Initialize variables, open file and read first record.  */

    nsegs = 0;
    npts = -1;
    total = -1;
    paddr = -1;
    pcaddr = -1;
    lats = 0;
    lons = 0;
    lnbias = 0;

    // DWF added error checking--protect 512-char buffer.
    assert (dir);
    assert (strlen(dir) + strlen(DIR_PATH_CHAR) + 11 < 512);


    /*  Have we changed files?  */

    if (strcmp (file, prev_file))
    {
        strcpy (prev_file, file);


        /*  Was there a file already opened?  */

        if (fp != NULL) fclose (fp);
        fp = NULL;


        /*  Use the environment variable WVS_DIR to get the         */
        /*  directory name.                                         */
        /*                                                          */
        /*  To set the variable in csh use :                        */
        /*                                                          */
        /*      setenv WVS_DIR /usr/wvswdb                          */
        /*                                                          */
        /*  To set the variable in sh, bash, or ksh use :           */
        /*                                                          */
        /*      WVS_DIR=/usr/wvswdb                                 */
        /*      export WVS_DIR                                      */


        /*  Don't handle the CIA WDB II files (they suck anyway).  */

        if (!strcmp (file, "coasts.dat") ||
            !strcmp (file, "rivers.dat") ||
            !strcmp (file, "bounds.dat")) return (0);

        /*  Determine the resolution of the file so that we can go to lower
            resolution if it is not available.  */

        strcpy (tmpfil, file);
        for (i = 0 ; i < 6 ; ++i)
        {
            if (!strcmp (files[i], tmpfil))
            {
                // RWL Put in for Windoze

                sprintf (dirfil, "%s%s%s", dir, DIR_PATH_CHAR, tmpfil);
                fp = fopen (dirfil, "rb");


                /*  No lower resolution files were available, look for
                    higher.  */

                if (fp == NULL && i == 5)
                {
                    strcpy (tmpfil, file);
                    for (j = 5 ; j >= 0 ; --j)
                    {
                        if (!strcmp (files[j], tmpfil))
                        {
                            // RWL Put in for Windoze

                            sprintf (dirfil, "%s%s%s", dir, DIR_PATH_CHAR,
                                     tmpfil);
                            fp = fopen (dirfil, "rb");
                            if (fp != NULL) break;


                            /*  No files found.  */

                            if (!j) return (0);

                            strcpy (tmpfil, files[j - 1]);
                        }
                    }
                }

                if (fp != NULL) break;

                strcpy (tmpfil, files[i + 1]);
            }
        }


        /*  Get the file info (we actually looked at sub-setting these).  */

        // DWF added error checking
        if (fseek (fp, 0, 0)) {
          fprintf (stderr, "wvsrtv:  error on first fseek\n");
          perror (prev_file);
          exit (-1);
        }
        if (fread (bytbuf, PHYSIZ, 1, fp) < 1) {
          fprintf (stderr, "wvsrtv:  error on first fread\n");
          exit (-1);
        }
        logrec = bytbuf[3];
        fulrec = logrec - 4;
        /* version = bytbuf[4]; */
        ioff = bytbuf[5];
        slatf = bytbuf[6] * 256 + bytbuf[7];
        nlatf = bytbuf[8] * 256 + bytbuf[9];
        wlonf = bytbuf[10] * 256 + bytbuf[11];
        elonf = bytbuf[12] * 256 + bytbuf[13];
        if (elonf < wlonf) elonf += 360;
        if (slatf + nlatf + wlonf + elonf == 0)
        {
            nlatf = 180;
            elonf = 360;
        }
        widef = elonf - wlonf;
        size = (nlatf - slatf) * (long) widef;
        todeg = 3600.0 * ioff;
        offset = (size - 1) / (logrec*8) + 2;
        lperp = PHYSIZ / logrec;
    }


    /*  Check for longitude entered in 0-360 world.  */

    if (lond > 180) lnbias = 360;


    /*  Compute integer values for retrieval and adjust if necessary. */

    lat = latd + 90;
    lon = lond;


    col = lon % 360;
    if (col < -180) col = col + 360;
    if (col >= 180) col = col - 360;
    col += 180;
    if (col < wlonf) col += 360;
    rindex = (lat - slatf) * (long) widef + (col - wlonf) + 1 + offset;


    /*  Check for cell outside of file area or no data.  */

    if (lat < slatf || lat >= nlatf || col < wlonf || col >= elonf ||
        !(celchk (rindex, logrec, offset, fp, &pcaddr))) return (0);


    /*  Compute physical record address, read record and save as previous
        address.  */

    eflag = false;
    addr = ((rindex - 1) / lperp) * PHYSIZ;
    if (addr != paddr)
    {
      // DWF added error checking
      if (fseek (fp, addr, 0)) {
        fprintf (stderr, "wvsrtv:  error on second fseek\n");
        perror (prev_file);
        exit (-1);
      }
      if (fread (bytbuf, PHYSIZ, 1, fp) < 1) {
        fprintf (stderr, "wvsrtv:  error on second fread\n");
        exit (-1);
      }
    }
    paddr = addr;


    /*  Compute byte position within physical record.  */

    curpos = ((rindex - 1) % lperp) * logrec;

    /*  If not at end of segment, process the record.  */

    while (!eflag)
    {
        /*  Get first two bytes of header and break out count and
            continuation bit.  */

        segcnt = (bytbuf[curpos] % 128) * 4 + bytbuf[curpos + 1] / 64 + 1;
        cont = bytbuf[curpos] / 128;

        /*  If this is a continuation record get offsets from the second
            byte.  */

        if (cont)
        {
            latoff = ((bytbuf[curpos + 1] % 64) / 8) * (long) 65536;
            lonoff = (bytbuf[curpos + 1] % 8) * (long) 65536;
        }


        /*  If this is an initial record set the offsets to zero.  */

        else
        {
            latoff = 0;
            lonoff = 0;
            // Additional in WVS1993 decoder
            // short rank = bytbuf[curpos+1]%64;
            // fprintf (stderr, "rank = %d\n", rank);
        }


        /*  Update the current byte position and get a new record if
            necessary.  */

        movpos (&rindex, bytbuf, &curpos, &addr, lperp, &paddr, fp, logrec,
            fulrec);


        /*  Compute the rest of the latitude offset.  */

        latoff += bytbuf[curpos] * (long) 256 + bytbuf[curpos + 1];

        movpos (&rindex, bytbuf, &curpos, &addr, lperp, &paddr, fp, logrec,
            fulrec);


        /*  Compute the rest of the longitude offset.  */

        lonoff += bytbuf[curpos] * (long) 256 + bytbuf[curpos + 1];


        /*  If this is a continuation record, bias the lat and lon offsets
            and compute the position.  */

        if (cont)
        {
            latoff -= 262144;
            lonoff -= 262144;
            lats += latoff;
            lons += lonoff;
        }


        /*  Else, compute the position.             */

        else
        {
            lats = (int) (lat * todeg) + latoff;
            lons = (int) (col * todeg) + lonoff;
        }


        /*  Set the position.  */

        dlat = (float) lats / todeg;
        dlon = (float) lons / todeg;


        /*  Update the current byte position.       */

        curpos += 2;


        /*  Get the continuation pointer.           */

        conbyt = ((rindex-1) % lperp) * logrec + fulrec;


        /*  If there is no continuation pointer or the byte position is
            not at the position pointed to by the continuation pointer,
            process the segment data.  */

        if (bytbuf[conbyt] == 0 || (curpos + 1) % logrec <=
            bytbuf[conbyt])
        {
            /*  If at the end of the logical record, get the next record
                in the chain.  */

            if (curpos % logrec == fulrec && bytbuf[conbyt] == 0)
                nxtrec (&rindex, bytbuf, &curpos, &addr, lperp, &paddr,
                fp, logrec);

            build_seg (dlat, dlon, &cont, &nsegs, &npts, &total, latray,
                lonray, segray, lnbias);


            /*  If the end of the segment has been reached, set the end
                flag.  */

            if ((curpos + 1) % logrec == bytbuf[conbyt]) eflag = true;


            /*  Process the segment.                */

            for (cnt = 2 ; cnt <= segcnt ; ++cnt)
            {

                /*  Compute the position from the delta record.  */

                latsec = bytbuf[curpos] - 128;
                lats += latsec;
                dlat = (float) lats / todeg;
                lonsec = bytbuf[curpos + 1] - 128;
                lons += lonsec;
                dlon = (float) lons / todeg;


                build_seg (dlat, dlon, &cont, &nsegs, &npts, &total, latray,
                    lonray, segray, lnbias);


                curpos += 2;

                conbyt = ((rindex - 1) % lperp) * logrec + fulrec;


                /*  If the end of the segment has been reached, set the
                    end flag and break out of for loop.  */

                if ((curpos + 1) % logrec == bytbuf[conbyt])
                {
                    eflag = true;
                    break;
                }
                else
                {
                    if (curpos % logrec == fulrec) nxtrec (&rindex,
                        bytbuf, &curpos, &addr, lperp, &paddr, fp, logrec);
                }
            }
        }

        /*  Break out of while loop if at the end of the segment.  */

        else
        {
            break;
        }                           /*  end if      */
    }                               /*  end while   */


    /*  Call the build_seg routine to flush the buffers.  */

    cont = 0;
    build_seg (999.0, 999.0, &cont, &nsegs, &npts, &total, latray, lonray,
        segray, lnbias);


    return (nsegs);
}
