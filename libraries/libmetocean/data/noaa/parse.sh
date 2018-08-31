#!/bin/bash


IFS=$'\r\n' GLOBIGNORE='*'
dat=($(cat stations2.txt))
nlines=${#dat[@]}
nsta=$(expr $nlines / 2)

for i in $(seq 0 2 $nlines)
do

    echo "Station $(expr $i / 2 + 1) of $nsta..."

    st=$(echo ${dat[$i]} | cut -d\> -f2 | cut -d\< -f1)
    id=$(echo $st | cut -c1-8)
    name=$(echo $st | cut -c9-)
    d=$(echo ${dat[$i+1]} | cut -d\> -f3 | cut -d\< -f1)
    st=$(echo $d | cut -d\- -f1)
    en=$(echo $d | cut -d\- -f2)

    if [ x$st == "x" ] ; then
        st="null"
    fi

    if [ x$en == "x" ] ; then
        en="null"
    fi

    if [ x$name == "x" ] ; then
        echo "ERROR: $id"
    fi

    if [ x$id == "x" ] ; then
        echo "ERROR: $name"
    fi

    id=$(echo -e "${id}" | tr -d '[:space:]')
    st="$(echo -e "${st}" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
    en="$(echo -e "${en}" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"

    #...Get the station location
    sthome="https://tidesandcurrents.noaa.gov/stationhome.html?id=$id#info"
    wget -o log.txt -O st.html $sthome 2>/dev/null

    lonline=$(cat st.html | grep Longitude)
    latline=$(cat st.html | grep Latitude)

    lat=$(echo $latline | cut -d\> -f4 | cut -d\< -f1)
    lat1=$(echo $lat | cut -d\& -f1)
    lat2=$(echo $lat | cut -d\; -f2 | cut -d\' -f1)
    lat2=$(echo "scale=8;$lat2 / 60" | bc)
    latns=$(echo $lat | cut -d\' -f2)
    if [ $latns == ' S' ] ; then
        lat1=-$lat1
    fi
    lat=$lat1$lat2

    
    lon=$(echo $lonline | cut -d\> -f4 | cut -d\< -f1)
    lon1=$(echo $lon | cut -d\& -f1)
    lon2=$(echo $lon | cut -d\; -f2 | cut -d\' -f1)
    lon2=$(echo "scale=8;$lon2 / 60" | bc)
    lonew=$(echo $lon | cut -d\' -f2)
    if [ $lonew == ' W' ] ; then
        lon1=-$lon1
    fi
    lon=$lon1$lon2

    echo "$id;$name;$lon;$lat;$st;$en" >> noaa.txt

    rm log.txt st.html

done
