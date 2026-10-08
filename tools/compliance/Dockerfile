# check=skip=InvalidDefaultArgInFrom
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# The format-compliance checkers (docs/station-netcdf.md 14.2): the IOOS
# compliance-checker, the CF checker (cfchecks), xarray, and netCDF-C's ncdump,
# on the Ubuntu of the dev image. Kept apart from the dev image, which has no
# use for them. tools/check_station_netcdf.sh builds and runs it; the
# format-compliance CI job installs the same pieces on its runner
# (tools/compliance/setup.sh).

ARG UBUNTU_IMAGE
FROM ${UBUNTU_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

COPY requirements.txt setup.sh /opt/compliance/
RUN /opt/compliance/setup.sh /opt/compliance \
    && rm -rf /var/lib/apt/lists/*
ENV PATH=/opt/compliance/venv/bin:$PATH \
    UDUNITS2_XML_PATH=/usr/share/xml/udunits/udunits2.xml \
    CF_TABLES=/opt/compliance

WORKDIR /work
CMD ["bash"]
