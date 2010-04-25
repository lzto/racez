#!/bin/bash
# This file is the driver for running httpd with racez

# global variables
CUR_DIR=`pwd`
MYSQL_TEST_BIN=/home/tianwei/mysql-server/5.0/mysql-test/
MYSQLD_BIN=/home/tianwei/mysql-server/5.0/sql/
MYSQLD_SOURCE=/home/tianwei/mysql-server/5.0/
RACEZ_DIR=/home/tianwei/my-repos/racez/racez-preload
RACEZ_TOOL_DIR=/home/tianwei/my-repos/racez/tools
FINAL_RESULT=$CUR_DIR/result
FINAL_TXT=$CUR_DIR/final.txt
CC_CMP="gcc -B/home/tianwei/mao/scripts -Wa,--mao=CFG:INSTSIM=trace+pmuprofile_file[$CUR_DIR/final.txt]+racez_raw_file[$CUR_DIR/b.txt]"
CXX_CMP="g++ -B/home/tianwei/mao/scripts -Wa,--mao=CFG:INSTSIM=trace+pmuprofile_file[$CUR_DIR/final.txt]+racez_raw_file[$CUR_DIR/b.txt]"
FOUND=0
FOUND2=0
COUNT=0
FIX_COUNT=0
count=0

if [ $# -lt 2 ]
then
echo "wrong useage, please try ./b.sh 1 racez/tsan"
exit 0
fi
TOOLS=$2
COUNT=$1
echo $TOOLS
echo $COUNT

run_bench_small()
{
killall -9 mysqld
if [ $TOOLS = "racez" ]
then
echo "running racez"
LD_PRELOAD=/home/tianwei/my-repos/racez/racez-preload/libracez-small.so /home/tianwei/mysql-server/5.0/sql/mysqld  --no-defaults --basedir=/home/tianwei/mysql-server/5.0/mysql-test --character-sets-dir=/home/tianwei/mysql-server/5.0/sql/share/charsets --secure-file-priv=/home/tianwei/mysql-server/5.0/mysql-test/var --log-bin-trust-function-creators --default-character-set=latin1 --language=/home/tianwei/mysql-server/5.0/sql/share/english --tmpdir=/home/tianwei/mysql-server/5.0/mysql-test/var/tmp --connect-timeout=60 --pid-file=/home/tianwei/mysql-server/5.0/mysql-test/var/run/master.pid --port=9306 --socket=/home/tianwei/mysql-server/5.0/mysql-test/var/tmp/master.sock --datadir=/home/tianwei/mysql-server/5.0/mysql-test/var/master-data --log=/home/tianwei/mysql-server/5.0/mysql-test/var/log/master.log --log-slow-queries=/home/tianwei/mysql-server/5.0/mysql-test/var/log/master-slow.log --log-bin=/home/tianwei/mysql-server/5.0/mysql-test/var/log/master-bin --server-id=1 --innodb_data_file_path=ibdata1:10M:autoextend --local-infile --skip-innodb --skip-ndbcluster --key_buffer_size=1M --sort_buffer=256K --max_heap_table_size=1M --core-file --open-files-limit=1024 &
else
/home/tianwei/valgrind/tsan_inst_tmp/bin/valgrind --tool=tsan --log-file=temp.log /home/tianwei/mysql-server/bin/libexec/mysqld  --no-defaults --basedir=/home/tianwei/mysql-server/5.0/mysql-test --character-sets-dir=/home/tianwei/mysql-server/5.0/sql/share/charsets --secure-file-priv=/home/tianwei/mysql-server/5.0/mysql-test/var --log-bin-trust-function-creators --default-character-set=latin1 --language=/home/tianwei/mysql-server/5.0/sql/share/english --tmpdir=/home/tianwei/mysql-server/5.0/mysql-test/var/tmp --connect-timeout=60 --pid-file=/home/tianwei/mysql-server/5.0/mysql-test/var/run/master.pid --datadir=/home/tianwei/mysql-server/bin/var --log=/home/tianwei/mysql-server/5.0/mysql-test/var/log/master.log --log-slow-queries=/home/tianwei/mysql-server/5.0/mysql-test/var/log/master-slow.log --log-bin=/home/tianwei/mysql-server/5.0/mysql-test/var/log/master-bin --server-id=1 --innodb_data_file_path=ibdata1:10M:autoextend --local-infile --skip-ndbcluster --key_buffer_size=1M --sort_buffer=256K --max_heap_table_size=1M --core-file --open-files-limit=1024 &
fi
# sleep somewhile to write the server to start
sleep 1
}

run_bench_large()
{ 
killall -9 mysqld
if [ $TOOLS = "racez" ]
then
echo "running racez"
LD_PRELOAD=/home/tianwei/my-repos/racez/racez-preload/libracez-large.so /home/tianwei/mysql-server/5.0/sql/mysqld  --no-defaults --basedir=/home/tianwei/mysql-server/5.0/mysql-test --character-sets-dir=/home/tianwei/mysql-server/5.0/sql/share/charsets --secure-file-priv=/home/tianwei/mysql-server/5.0/mysql-test/var --log-bin-trust-function-creators --default-character-set=latin1 --language=/home/tianwei/mysql-server/5.0/sql/share/english --tmpdir=/home/tianwei/mysql-server/5.0/mysql-test/var/tmp --connect-timeout=60 --pid-file=/home/tianwei/mysql-server/5.0/mysql-test/var/run/master.pid --port=9306 --socket=/home/tianwei/mysql-server/5.0/mysql-test/var/tmp/master.sock --datadir=/home/tianwei/mysql-server/5.0/mysql-test/var/master-data --log=/home/tianwei/mysql-server/5.0/mysql-test/var/log/master.log --log-slow-queries=/home/tianwei/mysql-server/5.0/mysql-test/var/log/master-slow.log --log-bin=/home/tianwei/mysql-server/5.0/mysql-test/var/log/master-bin --server-id=1 --innodb_data_file_path=ibdata1:10M:autoextend --local-infile --skip-innodb --skip-ndbcluster --key_buffer_size=1M --sort_buffer=256K --max_heap_table_size=1M --core-file --open-files-limit=1024 &
else 
/home/tianwei/valgrind/tsan_inst_tmp/bin/valgrind --tool=tsan --log-file=temp.log /home/tianwei/mysql-server/bin/libexec/mysqld  --no-defaults --basedir=/home/tianwei/mysql-server/5.0/mysql-test --character-sets-dir=/home/tianwei/mysql-server/5.0/sql/share/charsets --secure-file-priv=/home/tianwei/mysql-server/5.0/mysql-test/var --log-bin-trust-function-creators --default-character-set=latin1 --language=/home/tianwei/mysql-server/5.0/sql/share/english --tmpdir=/home/tianwei/mysql-server/5.0/mysql-test/var/tmp --connect-timeout=60 --pid-file=/home/tianwei/mysql-server/5.0/mysql-test/var/run/master.pid --datadir=/home/tianwei/mysql-server/bin/var --log=/home/tianwei/mysql-server/5.0/mysql-test/var/log/master.log --log-slow-queries=/home/tianwei/mysql-server/5.0/mysql-test/var/log/master-slow.log --log-bin=/home/tianwei/mysql-server/5.0/mysql-test/var/log/master-bin --server-id=1 --innodb_data_file_path=ibdata1:10M:autoextend --local-infile --skip-ndbcluster --key_buffer_size=1M --sort_buffer=256K --max_heap_table_size=1M --core-file --open-files-limit=1024 & 
fi
# sleep somewhile to write the server to start
sleep 2
}

testing_small()
{
cd $MYSQL_TEST_BIN
for ((i = 0; i < $COUNT; i++ )); do
rm /home/tianwei/mysql-server/bin/var/test/t1.*
./mysql-test-run --extern  --force --socket=/home/tianwei/mysql-server/5.0/mysql-test/var/tmp/master.sock tianwei-select-small --user=root
done;
}

testing_large()
{
cd $MYSQL_TEST_BIN
for ((i = 0; i < $COUNT; i++ )); do
rm /home/tianwei/mysql-server/bin/var/test/t1.*
./mysql-test-run --extern  --force --socket=/home/tianwei/mysql-server/5.0/mysql-test/var/tmp/master.sock tianwei-select-large --user=root
done;
}

testing()
{
cd $MYSQL_TEST_BIN
for ((i = 0; i < $COUNT; i++ )); do
#./mysql-test-run --extern  --force --do-test=lock --socket=/home/tianwei/mysql-server/5.0/mysql-test/var/tmp/master.sock --user=root
#rm /home/tianwei/mysql-server/bin/var/test/t1.* 
#./mysql-test-run --extern  --force --do-test=fulltext --socket=/home/tianwei/mysql-server/5.0/mysql-test/var/tmp/master.sock --user=root
rm /home/tianwei/mysql-server/bin/var/test/t1.* 
./mysql-test-run --extern  --force  insert --socket=/home/tianwei/mysql-server/5.0/mysql-test/var/tmp/master.sock --user=root
#rm /home/tianwei/mysql-server/bin/var/test/t1.* 
#./mysql-test-run --extern  --force --do-test=join --socket=/home/tianwei/mysql-server/5.0/mysql-test/var/tmp/master.sock --user=root
#rm /home/tianwei/mysql-server/bin/var/test/t1.* 
#./mysql-test-run --extern  --force --do-test=index --socket=/home/tianwei/mysql-server/5.0/mysql-test/var/tmp/master.sock --user=root
#rm /home/tianwei/mysql-server/bin/var/test/t1.* 
#./mysql-test-run --extern  --force --do-test=key --socket=/home/tianwei/mysql-server/5.0/mysql-test/var/tmp/master.sock --user=root
#rm /home/tianwei/mysql-server/bin/var/test/t1.* 
#./mysql-test-run --extern  --force --do-test=flush --user=root
#rm /home/tianwei/mysql-server/bin/var/test/t1.* 
#./mysql-test-run --extern  --force --do-test=innodb --user=root
#rm /home/tianwei/mysql-server/bin/var/test/t1.* 
#./mysql-test-run --extern  --force --socket=/home/tianwei/mysql-server/5.0/mysql-test/var/tmp/master.sock tianwei-qc --user=root
done;
}

shutdown_small()
{
 #shutdown the server
/home/tianwei/mysql-server/bin/bin/mysqladmin shutdown --socket=/home/tianwei/mysql-server/5.0/mysql-test/var/tmp/master.sock --user=root
sleep 10 
}

shutdown_large()
{
 #shutdown the server
/home/tianwei/mysql-server/bin/bin/mysqladmin shutdown --socket=/home/tianwei/mysql-server/5.0/mysql-test/var/tmp/master.sock --user=root
sleep 10
}

collecting()
{
   # The output file from racez
   echo "collecting the result"
   cd $CUR_DIR
   rm *.txt
   mv /home/tianwei/b.txt .
   cp $MYSQLD_BIN/mysqld . 
   $RACEZ_TOOL_DIR/convert b.txt mysqld final.txt

}

mao_processing() {
   cd $MYSQLD_SOURCE
#   if [ -f myisam/mi_locking.o ]; then
#      rm myisam/mi_locking.o
#   fi
   if [ -f mysys/thr_lock.o ]; then
      rm mysys/thr_lock.o
   fi
   CC=$CC_CMP CXX=$CXX_CMP make
}

mao_processing_1() {
   cd $MYSQLD_SOURCE
   if [ -f sql/ha_myisam.o ]; then
      rm sql/ha_myisam.o
   fi
   CC=$CC_CMP CXX=$CXX_CMP make
}

#final detecting
detecting()
{
  echo "detecting and reporting final result "
  cd $CUR_DIR
  mv /tmp/new.txt .
  $RACEZ_TOOL_DIR/detect -b mysqld -i b.txt -n new.txt -o result
}

analyzing_1()
{
  if grep "ha_myisam.cc:656" $FINAL_TXT > /dev/null
  then
    FOUND=1
  fi
}

analyzing_3()
{
  cd $CUR_DIR
  if grep "ha_myisam.cc:656" $FINAL_RESULT > /dev/null
  then
   count=$((count+1))
  fi

}

analyzing_4()
{
  cd $CUR_DIR
  if grep "thr_lock.c:489" $FINAL_RESULT > /dev/null
  then
    if grep "thr_lock.c:952" $FINAL_RESULT > /dev/null
    then
    count=$((count+1))
    fi
  fi
}

analyzing_2()
{
  cd $CUR_DIR
  if grep "ha_myisam.cc:1982" $FINAL_RESULT > /dev/null
  then
    if grep "mi_locking.c:313" $FINAL_RESULT > /dev/null
    then
    count=$((count+1)) 
    fi
  fi

}

run()
{
  run_bench_small
  testing
  shutdown_small
  collecting
  mao_processing_full
  detecting
}

main_small()
{
 FIX_COUNT=0
 count=0
 while [ $FIX_COUNT -le 40 ]
 do
    FIX_COUNT=$((FIX_COUNT+1))
    run_bench_small
    testing_small
    shutdown_small
    collecting
    mao_processing
    detecting
    analyzing_4
 done
 echo "total count to detect this bug is: "$count > small
}

main_large()
{
 FIX_COUNT=0
 count=0
 while [ $FIX_COUNT -le 80 ]
 do
    FIX_COUNT=$((FIX_COUNT+1))
    run_bench_large
    testing_large
    shutdown_large
    collecting
    mao_processing
    detecting
    analyzing_4
 done
 echo "total count to detect this bug is: "$count > large
}

echo "****Start testing mysqld with racez******"
main_small
main_large
