#-------------------------------GPL-------------------------------------#
#
# MetOcean Viewer - A simple interface for viewing hydrodynamic model data
# Copyright (C) 2015-2017  Zach Cobell
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
#-----------------------------------------------------------------------#

QT          += core

TARGET       = movProj4

TEMPLATE     = lib

PROJ4PATH    = $$PWD/../../thirdparty/proj4/src

INCLUDEPATH += $$PROJ4PATH

DEFINES      = PROJ4_LIBRARY

CONFIG      += staticlib

#...PROJ Sources
SOURCES_PROJ4 = \
    $$PROJ4PATH/pj_list.h $$PROJ4PATH/PJ_aeqd.c $$PROJ4PATH/PJ_gnom.c $$PROJ4PATH/PJ_laea.c $$PROJ4PATH/PJ_mod_ster.c \
    $$PROJ4PATH/PJ_nsper.c $$PROJ4PATH/PJ_nzmg.c $$PROJ4PATH/PJ_ortho.c $$PROJ4PATH/PJ_stere.c $$PROJ4PATH/PJ_sterea.c \
    $$PROJ4PATH/PJ_aea.c $$PROJ4PATH/PJ_bipc.c $$PROJ4PATH/PJ_bonne.c $$PROJ4PATH/PJ_eqdc.c $$PROJ4PATH/PJ_isea.c \
    $$PROJ4PATH/PJ_imw_p.c $$PROJ4PATH/PJ_krovak.c $$PROJ4PATH/PJ_lcc.c $$PROJ4PATH/PJ_poly.c $$PROJ4PATH/PJ_rpoly.c \
    $$PROJ4PATH/PJ_sconics.c $$PROJ4PATH/proj_rouss.c $$PROJ4PATH/PJ_cass.c $$PROJ4PATH/PJ_cc.c $$PROJ4PATH/PJ_cea.c \
    $$PROJ4PATH/PJ_eqc.c $$PROJ4PATH/PJ_gall.c $$PROJ4PATH/PJ_labrd.c $$PROJ4PATH/PJ_lsat.c $$PROJ4PATH/PJ_merc.c \
    $$PROJ4PATH/PJ_mill.c $$PROJ4PATH/PJ_ocea.c $$PROJ4PATH/PJ_omerc.c $$PROJ4PATH/PJ_somerc.c $$PROJ4PATH/PJ_tcc.c \
    $$PROJ4PATH/PJ_tcea.c $$PROJ4PATH/PJ_tmerc.c $$PROJ4PATH/PJ_airy.c $$PROJ4PATH/PJ_aitoff.c $$PROJ4PATH/PJ_august.c \
    $$PROJ4PATH/PJ_bacon.c $$PROJ4PATH/PJ_chamb.c $$PROJ4PATH/PJ_hammer.c $$PROJ4PATH/PJ_lagrng.c $$PROJ4PATH/PJ_larr.c \
    $$PROJ4PATH/PJ_lask.c $$PROJ4PATH/PJ_nocol.c $$PROJ4PATH/PJ_ob_tran.c $$PROJ4PATH/PJ_oea.c $$PROJ4PATH/PJ_tpeqd.c \
    $$PROJ4PATH/PJ_vandg.c $$PROJ4PATH/PJ_vandg2.c $$PROJ4PATH/PJ_vandg4.c $$PROJ4PATH/PJ_wag7.c $$PROJ4PATH/PJ_lcca.c \
    $$PROJ4PATH/PJ_geos.c $$PROJ4PATH/proj_etmerc.c $$PROJ4PATH/PJ_boggs.c $$PROJ4PATH/PJ_collg.c $$PROJ4PATH/PJ_crast.c \
    $$PROJ4PATH/PJ_denoy.c $$PROJ4PATH/PJ_eck1.c $$PROJ4PATH/PJ_eck2.c $$PROJ4PATH/PJ_eck3.c $$PROJ4PATH/PJ_eck4.c \
    $$PROJ4PATH/PJ_eck5.c $$PROJ4PATH/PJ_fahey.c $$PROJ4PATH/PJ_fouc_s.c $$PROJ4PATH/PJ_gins8.c $$PROJ4PATH/PJ_gstmerc.c \
    $$PROJ4PATH/PJ_gn_sinu.c $$PROJ4PATH/PJ_goode.c $$PROJ4PATH/PJ_igh.c $$PROJ4PATH/PJ_hatano.c $$PROJ4PATH/PJ_loxim.c \
    $$PROJ4PATH/PJ_mbt_fps.c $$PROJ4PATH/PJ_mbtfpp.c $$PROJ4PATH/PJ_mbtfpq.c $$PROJ4PATH/PJ_moll.c $$PROJ4PATH/PJ_nell.c \
    $$PROJ4PATH/PJ_nell_h.c $$PROJ4PATH/PJ_putp2.c $$PROJ4PATH/PJ_putp3.c $$PROJ4PATH/PJ_putp4p.c $$PROJ4PATH/PJ_putp5.c \
    $$PROJ4PATH/PJ_putp6.c $$PROJ4PATH/PJ_qsc.c $$PROJ4PATH/PJ_robin.c $$PROJ4PATH/PJ_sts.c $$PROJ4PATH/PJ_urm5.c \
    $$PROJ4PATH/PJ_urmfps.c $$PROJ4PATH/PJ_wag2.c $$PROJ4PATH/PJ_wag3.c $$PROJ4PATH/PJ_wink1.c $$PROJ4PATH/PJ_wink2.c \
    $$PROJ4PATH/pj_latlong.c $$PROJ4PATH/pj_geocent.c $$PROJ4PATH/aasincos.c $$PROJ4PATH/adjlon.c $$PROJ4PATH/bch2bps.c \
    $$PROJ4PATH/bchgen.c $$PROJ4PATH/biveval.c $$PROJ4PATH/dmstor.c $$PROJ4PATH/mk_cheby.c $$PROJ4PATH/pj_auth.c \
    $$PROJ4PATH/pj_deriv.c $$PROJ4PATH/pj_ell_set.c $$PROJ4PATH/pj_ellps.c $$PROJ4PATH/pj_errno.c $$PROJ4PATH/pj_factors.c \
    $$PROJ4PATH/pj_fwd.c $$PROJ4PATH/pj_init.c $$PROJ4PATH/pj_inv.c $$PROJ4PATH/pj_list.c $$PROJ4PATH/pj_malloc.c \
    $$PROJ4PATH/pj_mlfn.c $$PROJ4PATH/pj_msfn.c $$PROJ4PATH/proj_mdist.c $$PROJ4PATH/pj_open_lib.c $$PROJ4PATH/pj_param.c \
    $$PROJ4PATH/pj_phi2.c $$PROJ4PATH/pj_pr_list.c $$PROJ4PATH/pj_qsfn.c $$PROJ4PATH/pj_strerrno.c $$PROJ4PATH/pj_tsfn.c \
    $$PROJ4PATH/pj_units.c $$PROJ4PATH/pj_ctx.c $$PROJ4PATH/pj_log.c $$PROJ4PATH/pj_zpoly1.c $$PROJ4PATH/rtodms.c \
    $$PROJ4PATH/vector1.c $$PROJ4PATH/pj_release.c $$PROJ4PATH/pj_gauss.c $$PROJ4PATH/PJ_healpix.c $$PROJ4PATH/PJ_natearth.c \
    $$PROJ4PATH/PJ_calcofi.c $$PROJ4PATH/pj_fileapi.c $$PROJ4PATH/pj_gc_reader.c $$PROJ4PATH/pj_gridcatalog.c \
    $$PROJ4PATH/nad_cvt.c $$PROJ4PATH/nad_init.c $$PROJ4PATH/nad_intr.c $$PROJ4PATH/emess.c $$PROJ4PATH/emess.h \
    $$PROJ4PATH/pj_apply_gridshift.c $$PROJ4PATH/pj_datums.c $$PROJ4PATH/pj_datum_set.c $$PROJ4PATH/pj_transform.c \
    $$PROJ4PATH/geocent.c $$PROJ4PATH/geocent.h $$PROJ4PATH/pj_utils.c $$PROJ4PATH/pj_gridinfo.c $$PROJ4PATH/pj_gridlist.c \
    $$PROJ4PATH/jniproj.c $$PROJ4PATH/pj_mutex.c $$PROJ4PATH/pj_initcache.c $$PROJ4PATH/pj_apply_vgridshift.c $$PROJ4PATH/geodesic.c \
    $$PROJ4PATH/pj_strtod.c

SOURCES += proj4.cpp $$SOURCES_PROJ4 \
    epsg.cpp

HEADERS += proj4.h \
           proj4_global.h

