#!/bin/sh

unset XTTPD_ARG
for ARG in $* ; do
   case $ARG in
      *=* )
	eval export $ARG
        ;;
      * )
        XTTPD_ARG="$XTTPD_ARG $ARG"
	;;
   esac
done
			
exec /usr/libexec/xttpd $XTTPD_ARG