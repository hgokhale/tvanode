@echo off
set TARGET=configure build

if "%1"=="clean" set TARGET=clean
if "%1"=="rebuild" set TARGET=rebuild

node-gyp %TARGET%
