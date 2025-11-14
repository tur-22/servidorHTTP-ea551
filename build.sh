#!/bin/bash

if [ $# -lt 1 ] 
	then
	echo "Utilização: $0 <nome do servidor>"
	exit
fi

cd aux 
flex ../src/especifica.l
bison -d ../src/especifica.y
cd ..
gcc aux/especifica.tab.c aux/lex.yy.c src/process.c src/main/$1.c -o bin/servidor_$1 -lfl
