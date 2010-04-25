#!/bin/bash
# This file is the driver for running httpd with racez

# global variables
CUR_DIR=`pwd`
HTTPD_INSTALL=/home/tianwei/apache/install/
HTTPD_BIN=/home/tianwei/apache/install/bin
HTTPD_SOURCE=/home/tianwei/apache/httpd-2.2.14
RACEZ_DIR=/home/tianwei/my-repos/racez/racez-preload
RACEZ_TOOL_DIR=/home/tianwei/my-repos/racez/tools
FINAL_RESULT=$CUR_DIR/result
FINAL_TXT=$CUR_DIR/final.txt
COMP="gcc -B/home/tianwei/mao/scripts -Wa,--mao=CFG:INSTSIM=trace+pmuprofile_file[$CUR_DIR/final.txt]+racez_raw_file[$CUR_DIR/b.txt]"
FOUND=0
FOUND1=0
FOUND2=0
count=0
FIX_COUNT=0

#We need to do some cleanup work before start the httpd
cleanup ()
{
 cd $CUR_DIR
 killall -9 httpd
 for i in `ipcs -s # grep apache # awk '{print $2}'` ; do ipcrm -s $i; done 
}

# start the httpd with racez in backgroud
run_large ()
{ 
  cd $CUR_DIR
  rm -fr $HTTPD_INSTALL/logs/*
  #first run the httpd on port 8000
  LD_PRELOAD=$RACEZ_DIR/libracez-large.so $HTTPD_BIN/httpd -X -k start &
  sleep 1;
}

run_small ()
{
  cd $CUR_DIR
  rm -fr $HTTPD_INSTALL/logs/*
  #first run the httpd on port 8000
  RACEZ_SAMPLE_SKID=1 RACEZ_SIGNAL_SAMPLE=1 RACEZ_MEMORY_ALLOCATOR=1 RACEZ_WRITE_RECORD=1 LD_PRELOAD=$RACEZ_DIR/libracez.so $HTTPD_BIN/httpd -X -k start &
  sleep 1;
}

# send queries into the server
testing_large()
{
  cd $CUR_DIR
  SIZE=800000
  $HTTPD_BIN/ab -n $SIZE -c 20 http://localhost:8000/
  $HTTPD_BIN/apachectl -X -k stop
  sleep 3 
}

testing_small()
{
  cd $CUR_DIR
  SIZE=50000
  $HTTPD_BIN/ab -n $SIZE -c 20 http://localhost:8000/
  $HTTPD_BIN/apachectl -X -k stop
  sleep 60
}

# post-processing
collecting_3()
{
   # The output file from racez
   cd $CUR_DIR
   rm *.txt
   mv /home/tianwei/b.txt .
   cp $HTTPD_BIN/httpd .
   $RACEZ_TOOL_DIR/convert b.txt httpd final.txt
   cd $HTTPD_SOURCE
   if [ ! -f server/mpm/worker/worker.lo ]; then
       CC=$COMP make
   else
       rm server/mpm/worker/worker.lo && CC=$COMP make
   fi
}

collecting_5()
{
   # The output file from racez
   cd $CUR_DIR
   rm *.txt
   mv /home/tianwei/b.txt .
   cp $HTTPD_BIN/httpd .
   $RACEZ_TOOL_DIR/convert b.txt httpd final.txt
   cd $HTTPD_SOURCE
   if [ ! -f srclib/apr/memory/unix/apr_pools.lo ]; then
       CC=$COMP make
   else
       rm srclib/apr/memory/unix/apr_pools.lo && CC=$COMP make
   fi
}

# post-processing
collecting_2()
{
   # The output file from racez
   cd $CUR_DIR
   rm *.txt
   mv /home/tianwei/b.txt .
   cp $HTTPD_BIN/httpd . 
   $RACEZ_TOOL_DIR/convert b.txt httpd final.txt
   cd $HTTPD_SOURCE
   if [ ! -f server/mpm/worker/fdqueue.lo ]; then 
       CC=$COMP make
   else 
       rm server/mpm/worker/fdqueue.lo && CC=$COMP make
   fi
}

#final detecting
detecting()
{
  cd $CUR_DIR
  mv /tmp/new.txt .
  $RACEZ_TOOL_DIR/detect -b httpd -i b.txt -n new.txt -m -o result 
}

analyzing_2()
{
  cd $CUR_DIR
  if grep "fdqueue.c:102" $FINAL_RESULT > /dev/null
  then
    if grep "fdqueue.c:108" $FINAL_RESULT > /dev/null
    then
    count=$((count+1))
    fi
  fi

}

analyzing_5()
{
  cd $CUR_DIR
  if grep "apr_pools.c:737" $FINAL_RESULT > /dev/null
  then
    if grep "apr_pools.c:908" $FINAL_RESULT > /dev/null
    then
    count=$((count+1)) 
    fi
  fi

}

analyzing_1()
{
cd $CUR_DIR
for ip in `grep "fdqueue.c:108" $FINAL_TXT | awk '{print $4}'`;
do
   if [ $ip == 127 -o $ip == 131 ];
   then
      FOUND=1
      grep ap_queue_info_set_idle $FINAL_TXT >> bug1
   fi
done

for ip in `grep "fdqueue.c:108" $FINAL_TXT | awk '{print $4}'`;
do
   if [ $ip == 135 ];
   then
      FOUND=1
      grep ap_queue_info_set_idle $FINAL_TXT >> bug1
   fi
done

}

analyzing_xxxx()
{
cd $CUR_DIR
for ip in `grep "fdqueue.c:108" $FINAL_TXT | awk '{print $4}'`;
do
   if [ $ip == 127 -o $ip == 131 ];
   then
      FOUND2=1
      grep ap_queue_info_set_idle $FINAL_TXT >> bug1 
   fi
done

for ip in `grep "fdqueue.c:108" $FINAL_TXT | awk '{print $4}'`;
do
   if [ $ip == 135 ];
   then
      FOUND2=1
      grep ap_queue_info_set_idle $FINAL_TXT >> bug1 
   fi
done
for ip in `grep "fdqueue.c:102" $FINAL_TXT | awk '{print $4}'`;
do
   if [ $ip == 74 -o $ip == 78 ];
   then
      FOUND1=1
      grep ap_queue_info_set_idle $FINAL_TXT >> bug1 
   fi
done

for ip in `grep "fdqueue.c:104" $FINAL_TXT | awk '{print $4}'`;
do
   if [ $ip == 82 ];
   then
      FOUND1=1
      grep ap_queue_info_set_idle $FINAL_TXT >> bug1 
   fi
done
}

analyzing_3()
{
cd $CUR_DIR
FOUND1=0
FOUND2=0
for ip in `grep "worker.c:895" $FINAL_TXT | awk '{print $4}'`;
do
   if [ $ip == 614 -o $ip == 617 -o $ip == 620 -o $ip == 624 -o $ip == 627 ];
   then
      FOUND2=1
      grep worker_thread $FINAL_TXT >> bug1 
   fi
done

for ip in `grep "worker.c:896" $FINAL_TXT | awk '{print $4}'`;
do
   if [ $ip == 634 -o $ip == 640 -o $ip == 647 ];
   then
      FOUND2=1
      grep worker_thread $FINAL_TXT >> bug1 
   fi
done
for ip in `grep "worker.c:897" $FINAL_TXT | awk '{print $4}'`;
do
   if [ $ip == 653 -o $ip == 657 ];
   then
      FOUND2=1
      grep worker_thread $FINAL_TXT >> bug1 
   fi
done

for ip in `grep "worker.c:633" $FINAL_TXT | awk '{print $4}'`;
do
   if [ $ip == 296  -o $ip == 302 -o $ip == 304 ];
   then
      FOUND1=1
      grep listener_thread $FINAL_TXT >> bug1 
   fi
done
if [ $FOUND1 == 1 -a $FOUND2 == 1 ];
then
  count=$((count+1))   
fi
}

main_large()
{
 FIX_COUNT=0
 count=0
 while [ $FIX_COUNT -le 500 ]
 do
    FIX_COUNT=$((FIX_COUNT+1))
    cleanup
    run_large
    testing_large
    collecting_3
    #detecting
    analyzing_3
 done
 echo "total count to detect this bug is: "$count > large
}

main_small()
{
 FIX_COUNT=0
 count=0
 while [ $FIX_COUNT -le 0 ]
 do
    FIX_COUNT=$((FIX_COUNT+1))
    cleanup
    run_small
    testing_small
    collecting_3
    #detecting
    analyzing_3
 done
 echo "total count to detect this bug is: "$count > small
}

main()
{
 while [ $FIX_COUNT -le 100 ]
 do
    FIX_COUNT=$((FIX_COUNT+1))
    cleanup
    run
    testing
    collecting_2
    detecting
    analyzing_2    
 done
 echo "total count to detect this bug is: "$count
}

echo "****Start testing httpd with racez******"
echo "unit of query is : size="$size" c=50" 
main_small
#main_large
