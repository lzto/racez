# because of the spawn way of mysqld-test, we need to shit the command
export LD_PRELOAD=/home/tianwei/my-repos/racez/racez-preload/libracez.so
$@
