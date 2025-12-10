find meu-webspace -type d -exec chmod 755 {} \;
find meu-webspace -type f -exec chmod 644 {} \;

# Configura permissões dos arquivos do webspace para que possa ser zipado.
#
# Utilize setup_webspace.sh para preparar o webspace para testes.
