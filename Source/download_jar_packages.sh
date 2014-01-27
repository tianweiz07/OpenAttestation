#!/bin/bash

JAR_SOURCE_DIRCTORY=../JAR_SOURCE
DOWNLOAD_LOG=Download.log
[[ -d  $JAR_SOURCE_DIRCTORY ]] && rm -rf $JAR_SOURCE_DIRCTORY
  mkdir $JAR_SOURCE_DIRCTORY

[[ -f $DOWNLOAD_LOG ]] && rm -f $DOWNLOAD_LOG
  touch $DOWNLOAD_LOG

DOWNLOAD_CONTENT=`cat download_jar_package_list.txt`

for DOWNLOAD_FILE_NAME in $DOWNLOAD_CONTENT
do
  LOCAL_NAME=`echo "$DOWNLOAD_FILE_NAME" | awk -F"-----" '{print $1}'`
  DOWNLOAD_PATH=`echo "$DOWNLOAD_FILE_NAME" | awk -F"-----" '{print $2}'`
  echo "$LOCAL_NAME $DOWNLOAD_PATH" 
  #most downlaod times
  key=5
  while [ $key -gt 0 ]
  do
    key=$(expr $key - 1)
    wget  -t 1 -O ../JAR_SOURCE/$LOCAL_NAME $DOWNLOAD_PATH
    STAT=$?
    [ $STAT == 0 ] && break
    echo "Download file [ $LOCAL_NAME ] $(expr 5 - $key) th" >> $DOWNLOAD_LOG
    rm -f $JAR_SOURCE_DIRCTORY/$LOCAL_NAME
  done 

  if [ $key -eq 0 ];then
    echo "Download file [ $LOCAL_NAME ] from [ $DOWNLOAD_PATH ] failed!" >> $DOWNLOAD_LOG
    rm -f $JAR_SOURCE_DIRCTORY/$LOCAL_NAME
  fi

done

cd $JAR_SOURCE_DIRCTORY
cp commons-collections-2.1.1.jar commons-collections.jar 
cp commons-logging-1.0.4.jar commons-logging.jar
cp jaxb-impl-2.1.12.jar jaxb-impl.jar
cp ../bcprov-jdk15-143.jar bcprov-jdk15-129.jar 
mv ../bcprov-jdk15-143.jar bcprov-jdk15-141.jar

