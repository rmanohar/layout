#!/bin/sh

ARCH=`$VLSI_TOOLS_SRC/scripts/getarch`
OS=`$VLSI_TOOLS_SRC/scripts/getos`
EXT=${ARCH}_${OS}
ACTTOOL=../act2lef.$EXT 

if [ ! -d runs ]
then
	mkdir runs
fi

num=0
count=0
while [ -f ${count}.act ]
do
	i=${count}.act
	file=${count}
	count=`expr $count + 1`
	$ACTTOOL -p 'test<>' -c cells.act $i > runs/$i.stdout 2> runs/$i.stderr
	for i in out.lef out.def out.cell *.rect
	do
	    mv $i runs/${file}-${i}
	done
done
