
#/bin/bash

while read LINE
do
    c1=$(echo $LINE | cut -c1)
    if [ $c1 == "#" ] ; then
        continue
    fi

    pjnum=$(echo $LINE | cut -d\< -f2 | cut -d\> -f1)
    pjinit=$(echo $LINE | cut -d\> -f2 | cut -d\< -f1)
    pjinit=$(echo $pjinit)


#    echo "this->epsgMapping[$pjnum] = \"$pjinit\";"
    echo "        {$pjnum,\"$pjinit\"}"


done < epsg
