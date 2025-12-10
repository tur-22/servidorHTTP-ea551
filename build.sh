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
gcc especifica.tab.c lex.yy.c process.c $1.c -o ../bin/servidor_$1 -lfl -lssl -lcrypto -lcrypt
rm especifica.tab.c lex.yy.c especifica.tab.h

# INSTRUÇÕES PARA COMPILAÇÃO E EXECUÇÃO DO SERVIDOR MULTI THREAD
#
# Este script faz uso do comando gcc linkando o programa gerado com algumas bibliotecas, e
# portanto depende de elas estarem instaladas.
# 
# Mais especificamente, requer as bibliotecas openssl (lssl e lcrypto), que estão instaladas
# em paths não encontrados automaticamente pelo gcc nos computadores da LE-27. Para compilar
# o programa nestes computadores, utilize o script build_feec.sh.
#
# Passos:
#
# 1. Execute o script build.sh da seguinte forma:
#	./build.sh multi_thread
#
# 2. A partir do diretório deste arquivo, execute:
#	bin/servidor_multi_thread <porta> <N> <path absoluto para webspace> <path para registro.txt>
#
# OBS: os códigos-fonte das versões mono_select e multi_poll foram também foram enviados nesta
# atividade, por mais que não tenha sido pedido. Para compilá-los, execute este script desta
# forma: ./build.sh <nome do servidor> (ex: ./build.sh mono_select).
#
# OBS2: É necessário estar neste diretório para executar o programa do servidor, como indicado,
# no passo 2

