#!/bin/bash

echo "Latitude;Longitude;Type;StationID;StationName;MLLW;MLW;MSL;MHW;MHHW;NGVD;NAVD" 

xtide=xtide_stations.csv
mlw=result/xtide_stations_mlw.csv
msl=result/xtide_stations_lmsl.csv
mhw=result/xtide_stations_mhw.csv
mhhw=result/xtide_stations_mhhw.csv
ngvd=result/xtide_stations_ngvd29.csv
navd=result/xtide_stations_navd88.csv

readarray -t xtide_data < $xtide
readarray -t mlw   < $mlw  
readarray -t msl   < $msl  
readarray -t mhw   < $mhw  
readarray -t mhhw  < $mhhw 
readarray -t ngvd  < $ngvd 
readarray -t navd  < $navd 

nline=${#xtide_data[@]}
for((i=1;i<$nline;i++))
do
    xtide_line=${xtide_data[$i]}
    mlw_line=${mlw[$i]}
    msl_line=${msl[$i]}
    mhw_line=${mhw[$i]}
    mhhw_line=${mhhw[$i]}
    ngvd_line=${ngvd[$i]}
    navd_line=${navd[$i]}

    mlw_adj=$(echo $mlw_line | cut -d\; -f1)
    msl_adj=$(echo $msl_line | cut -d\; -f1)
    mhw_adj=$(echo $mhw_line | cut -d\; -f1)
    mhhw_adj=$(echo $mhhw_line | cut -d\; -f1)
    ngvd_adj=$(echo $ngvd_line | cut -d\; -f1)
    navd_adj=$(echo $navd_line | cut -d\; -f1)

    echo "$xtide_line;0.0;$mlw_adj;$msl_adj;$mhw_adj;$mhhw_adj;$ngvd_adj;$navd_adj"

done


