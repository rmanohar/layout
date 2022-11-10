#!/bin/sh

ARCH=`$ACT_HOME/scripts/getarch`
OS=`$ACT_HOME/scripts/getos`
EXT=${ARCH}_${OS}
if [ ! x$ACT_TEST_INSTALL = x ] || [ ! -f ../act2lef.$EXT ]; then
  ACTTOOL=$ACT_HOME/bin/act2lef
  echo "testing installation"
  echo
else
  ACTTOOL=../act2lef.$EXT
fi

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
	$ACTTOOL -cnf=m.conf -p 'test<>' -c cells.act $i > runs/$i.stdout 2> runs/$i.stderr
	for i in out.lef out.def out.cell *.rect
	do
	    mv $i runs/${file}-${i}
	done
done
