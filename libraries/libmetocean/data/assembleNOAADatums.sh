#!/bin/bash

noaa=noaa_stations.csv
mllw=result/noaa_stations_mllw.csv
mlw=result/noaa_stations_mlw.csv
mhw=result/noaa_stations_mhw.csv
mhhw=result/noaa_stations_mhhw.csv
ngvd=result/noaa_stations_ngvd29.csv
navd=result/noaa_stations_navd88.csv

readarray -t noaa_data < $noaa
readarray -t mllw   < $mllw  
readarray -t mlw   < $mlw  
readarray -t mhw   < $mhw  
readarray -t mhhw  < $mhhw 
readarray -t ngvd  < $ngvd 
readarray -t navd  < $navd 

nline=${#noaa_data[@]}
for((i=1;i<$nline;i++))
do
    xtide_line=${noaa_data[$i]}
    mllw_line=${mllw[$i]}
    mlw_line=${mlw[$i]}
    mhw_line=${mhw[$i]}
    mhhw_line=${mhhw[$i]}
    ngvd_line=${ngvd[$i]}
    navd_line=${navd[$i]}

    mllw_adj=$(echo $mllw_line | cut -d\; -f1)
    mlw_adj=$(echo $mlw_line | cut -d\; -f1)
    mhw_adj=$(echo $mhw_line | cut -d\; -f1)
    mhhw_adj=$(echo $mhhw_line | cut -d\; -f1)
    ngvd_adj=$(echo $ngvd_line | cut -d\; -f1)
    navd_adj=$(echo $navd_line | cut -d\; -f1)

    echo "$xtide_line;$mllw_adj;$mlw_adj;0.0;$mhw_adj;$mhhw_adj;$ngvd_adj;$navd_adj"

done


