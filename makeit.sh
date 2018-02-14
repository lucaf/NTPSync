#!/bin/bash

rm -f _NtpSyncPy.so
rm -rf build
rm -f *.pyc

if [ "$1" != "remove" ]; then
  swig -python NtpSyncPy.i
  python setup.py build
  find build -name "_NtpSyncPy.so" -exec cp {} . \;
fi
