#!/bin/bash
# This file is the driver for running httpd with racez

# global variables
CUR_DIR=`pwd`
BIN='pwd'
SOURCE='pwd'
RACEZ_DIR=/home/tianwei/my-repos/racez/racez-preload
RACEZ_TOOL_DIR=/home/tianwei/my-repos/racez/tools
FINAL_RESULT=$CUR_DIR/result
FINAL_TXT=$CUR_DIR/final.txt
COMP="gcc -B/home/tianwei/mao/scripts -Wa,--mao=CFG:INSTSIM=trace+pmuprofile_file[$CUR_DIR/final.txt]+racez_raw_file[$CUR_DIR/b.txt]"
FOUND=0
FOUND0=0
FOUND1=0
count=0
FIX_COUNT=0
building ()
{
 gcc -g -O0 -lpthread ./libracezutil.so -rdynamic -g test-1.c -o test-1 
}

cleanup ()
{
 rm *.txt
 rm result
}
# run the tool 
run ()
{ 
  LD_PRELOAD=$RACEZ_DIR/libracez.so ./test-1 
}

collecting()
{
   # The output file from racez
   mv /home/tianwei/b.txt .
   $RACEZ_TOOL_DIR/convert b.txt test-1 final.txt
   $COMP -g -O0 test-1.c -c
}

#final detecting
detecting()
{
  cd $CUR_DIR
  mv /tmp/new.txt .
  $RACEZ_TOOL_DIR/detect -b test-1 -i b.txt -n new.txt  -o result 
}

analyzing_2() {
  cd $CUR_DIR
  if grep "test-1.c:45" $FINAL_RESULT > /dev/null
  then
    if grep "test-1.c:84" $FINAL_RESULT > /dev/null
    then
    count=$((count+1)) 
    fi
  fi
}
analyzing()
{
cd $CUR_DIR
for ip in `grep "test-1.c:45" $FINAL_TXT | awk '{print $4}'`;
do
   if [ $ip == 44 ];
   then
      FOUND=1
      grep foo_2 $FINAL_TXT >> bug1
   fi
done
for ip in `grep "test-1.c:43" $FINAL_TXT | awk '{print $4}'`;
do
   if [ $ip == 50 -o $ip == 54 ];
   then
      FOUND=1
      grep foo_2 $FINAL_TXT >> bug1
   fi
done
}

analyzing_1()
{
ip=`grep "test-1.c:26" final.txt | awk '{print $4}'`
if [ $ip = 66 ];
then
 FOUND1=1
 cp b.txt save-a
 cp final.txt save-final-a
fi
ip=`grep "test-1.c:52" final.txt | awk '{print $4}'`
if [ $ip = 66 ];
then
 FOUND0=1
 cp b.txt save-b
 cp final.txt save-final-b
fi

}
main_1()
{
 rm save*
 building
 while [ $FIX_COUNT -le 1000 ]
 do
    FIX_COUNT=$((FIX_COUNT+1))
    cleanup
    run
    collecting
    detecting
    analyzing_2
 done
 #detecting save-a.txt save-b.txt
 echo "total count to detect this bug is: "$count
}

main()
{
 rm save*
 building
 while [ $FOUND0 = 0 -o $FOUND1 = 0 ]
 do
    count=$((count+1))
    cleanup 
    run
    collecting
    analyzing_1
 done
 #detecting save-a.txt save-b.txt
 echo "total count to detect this bug is: "$count
}

echo "****Start testing httpd with racez******"
main_1
