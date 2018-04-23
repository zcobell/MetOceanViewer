#!/bin/bash

perl -w -pi -e "s/\,\"/\;\"/g" $1
perl -w -pi -e "s/\"\,/\"\;/g" $1

for((i=0;i<=9;i++))
do
    perl -w -pi -e "s/$i\,/$i\;/g" $1
done
