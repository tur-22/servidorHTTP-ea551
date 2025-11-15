#!/bin/bash

if [ $# -lt 1 ] 
	then
	echo "Utilização: $0 <nome do servidor>"
	exit
fi

cd "$(dirname "$BASH_SOURCE[0]")" # leva pwd para raiz do repo

cd src
flex especifica.l
bison -d especifica.y
gcc especifica.tab.c lex.yy.c process.c $1.c -o ../bin/servidor_$1 -lfl
rm especifica.tab.c lex.yy.c especifica.tab.h
