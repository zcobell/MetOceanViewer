#!/bin/bash

while read LINE
do

    IFS=","
    i=0
    for COL in $LINE
    do
        data[$i]=$COL
        i=$(expr $i + 1)
    done
    
    abrev=${data[0]}
    name=$(echo ${data[1]} | xargs)
    country=${data[2]}
    offset=${data[3]}
#    offset=$(echo $offset | cut -d"/" -f1)

    offsetHour=$(echo $offset | cut -d":" -f1)
    offsetMin=$(echo $offset | cut -d":" -f2)

#    offsetHour=$(echo $offsetHour | tr -d '[:space:]')
#    offsetMin=$(echo $offsetMin | tr -d '[:space:]')

    if [ $offsetMin == $offsetHour ] ; then
        offsetMin=0
    fi

    offsetSeconds=$(echo "$offsetHour * 3600 + $offsetMin * 60" | bc)

    echo "this->m_timezones.insert(std::make_pair<TZ::Location, TZ::Abbreviation>($country,$abrev),TimezoneStruct($country,$abrev,std::stringLiteral(\"$abrev\"),std::stringLiteral(\"$name\"),std::stringLiteral(\"$country\"),$offsetSeconds));"

done < timezones.csv
