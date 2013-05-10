#!/bin/sh

cd `dirname "$0"`
source ./functions.sh 2>/dev/null
if [ $? != 0 ]; then
    . ./functions.sh
    if [ $? != 0 ]; then
        echo [ERROR] Can not include 'functions.sh'.
        exit 1
    fi
fi

#If install.sh in admin/misc, need to change directory
LSINSTALL_DIR=`dirname "$0"`
#cd $LSINSTALL_DIR/


init
INSTALL_TYPE="reinstall"
LSWS_HOME=$1
WS_USER=$2
WS_GROUP=$3
ADMIN_USER=$4
PASS_ONE=$5
ADMIN_EMAIL=$6

VERSION=open
HTTP_PORT=8088
ADMIN_PORT=7080
SETUP_PHP=1
PHP_SUFFIX="php"
SSL_HOSTNAME=""


DIR_OWN=$WS_USER:$WS_GROUP
CONF_OWN=$WS_USER:$WS_GROUP

configRuby


#Comment out the below two lines
echo "Target_Dir:$LSWS_HOME" "User:"$WS_USER "Group:$WS_GROUP" Admin:$ADMIN_USER Password:$PASS_ONE LSINSTALL_DIR:$LSINSTALL_DIR  END

echo
echo -e "\033[38;5;148mInstalling, please wait...\033[39m"
echo



if [ "$7" = "yes" ] ; then
    gen_selfsigned_cert ../adminssl.conf
    cp $LSINSTALL_DIR/${SSL_HOSTNAME}.crt $LSINSTALL_DIR/admin/conf/${SSL_HOSTNAME}.crt
    cp $LSINSTALL_DIR/${SSL_HOSTNAME}.key $LSINSTALL_DIR/admin/conf/${SSL_HOSTNAME}.key
fi

buildConfigFiles ${SSL_HOSTNAME}
installation

rm $LSWS_HOME/bin/lshttpd
ln -sf $LSWS_HOME/bin/openlitespeed $LSWS_HOME/bin/lshttpd

if [ -f "$LSWS_HOME/admin/fcgi-bin/admin_php" ]; then
    echo -e "\033[38;5;148mphp already exists, needn't to re-build\033[39m"
else
    echo -e "\033[38;5;148mStart to build php, this may take several minutes, please waiting ...\033[39m"
    $LSWS_HOME/admin/misc/build_admin_php.sh
fi


ENCRYPT_PASS=`"$LSWS_HOME/admin/fcgi-bin/admin_php" -q "$LSWS_HOME/admin/misc/htpasswd.php" $PASS_ONE`
echo "$ADMIN_USER:$ENCRYPT_PASS" > "$LSWS_HOME/admin/conf/htpasswd"



if [ -f "$LSWS_HOME/fcgi-bin/lsphp" ]; then
    mv -f "$LSWS_HOME/fcgi-bin/lsphp" "$LSWS_HOME/fcgi-bin/lsphp.old"
    echo "Your current PHP engine $LSWS_HOME/fcgi-bin/lsphp is renamed to lsphp.old"
fi

cp -f "$LSWS_HOME/admin/fcgi-bin/admin_php" "$LSWS_HOME/fcgi-bin/lsphp"
chown "$SDIR_OWN" "$LSWS_HOME/fcgi-bin/lsphp"
chmod "$EXEC_MOD" "$LSWS_HOME/fcgi-bin/lsphp"
if [ ! -f "$LSWS_HOME/fcgi-bin/lsphp5" ]; then
    ln -sf "./lsphp" "$LSWS_HOME/fcgi-bin/lsphp5"
fi


#compress_admin_file
if [ ! -f "$LSWS_HOME/admin/conf/jcryption_keypair" ]; then
    $LSWS_HOME/admin/misc/create_admin_keypair.sh
fi
chown "$CONF_OWN" "$LSWS_HOME/admin/conf/jcryption_keypair"
chmod 0600 "$LSWS_HOME/admin/conf/jcryption_keypair"


echo
echo -e "\033[38;5;148mInstallation finished, Enjoy!\033[39m"
echo


