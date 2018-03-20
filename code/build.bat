@echo off

mkdir ..\..\build
pushd ..\..\build
cl -FC -Zi w:\source\code\Win32_plat_layer.cpp User32.lib Gdi32.lib
popd