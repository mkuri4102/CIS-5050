#!/bin/bash

echo "#include <stdio.h>" >filter.c
echo "#include <string.h>" >>filter.c
echo "int main(void)" >>filter.c
echo "{" >>filter.c
echo "  char cutting = 0;" >>filter.c
echo "  char line[2000];" >>filter.c
echo "  while (fgets(line, sizeof(line), stdin)) {" >>filter.c
echo "    char *p = strstr(line, cutting ? \"//CUTEND\" : \"//CUTBEGIN\");" >>filter.c
echo "    if (p) {" >>filter.c
echo "      *p=0;" >>filter.c
echo "      cutting = 1-cutting;" >>filter.c
echo "    } else {" >>filter.c
echo "      if (!cutting)" >>filter.c
echo "        printf(\"%s\", line);" >>filter.c
echo "    }" >>filter.c
echo "  }" >>filter.c
echo "  return 0;" >>filter.c
echo "}" >>filter.c

echo "Compiling filter..."
gcc filter.c -o filter

echo "Preparing files..."
ROOT=HW1
rm -rf ./$ROOT
rm -f cis5050-hw1.zip
mkdir $ROOT
cp Makefile $ROOT
cp makeinput.cc $ROOT
cat mysort.cc |./filter >$ROOT/mysort.cc
cp README .cproject .project $ROOT/
cp -R .settings $ROOT/

echo "Building archive..."
zip -r cis5050-hw1.zip $ROOT

echo "Cleaning up..."
rm -rf ./$ROOT
rm filter.c filter
