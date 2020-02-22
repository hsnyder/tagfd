#!/bin/bash

#Important: make sure this file has unix line endings

grep -v "^\s*$" cfg/tagfd.conf | grep -v "^#.*$" | tr -d "\r" |
while read line
do
    bin/tfdconfig t $line
    if [[ $? -ne 0 ]]
    then 
        exit 1
    fi
done

grep -v "^\s*$" cfg/tagfd.conf | grep -v "^#.*$" | tr -d "\r" |
while read line
do
    bin/tfdconfig + $line
    if [[ $? -ne 0 ]]
    then 
        exit 1
    fi
done
