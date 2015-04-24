#!/bin/sh

#PARAMETERS:
# $1 - Compiled file name (SOME.DLL or SOME.EXE)

PRODUCT_NAME="Desktop Duplication API for Windows 7"
COMPANY_NAME="Jonas Kümmerlin"
COPYRIGHT="© 2015 Jonas Kümmerlin"
FILE_DESCRIPTION="$PRODUCT_NAME"
ORIGINAL_FILE_NAME="$1"
INTERNAL_NAME="$1"

# Build and patch is added automatically
MAJOR_VERSION=0
MINOR_VERSION=1
BUILD=$((`date "+(%Y-2000)*365+(%Y-2000)/4+(1%j-1000)"`))
REVISION=$((`date "+((1%H-100)*3600 + (1%M-100)*60 + (1%S-100))/2"`))
BUILDDATE=`date +"%D %H:%M:%S"`

case "$ORIGINAL_FILE_NAME" in
    *.dll|*.DLL)
        FILETYPE="VFT_DLL"
        ;;
    *.exe|*.EXE)
        FILETYPE="VFT_APP"
        ;;
    *)
        FILETYPE="VFT_UNKNOWN"
        ;;
esac

cat <<EOF
#include <windows.h>

LANGUAGE LANG_NEUTRAL, SUBLANG_NEUTRAL

VS_VERSION_INFO VERSIONINFO
  FILEVERSION       $MAJOR_VERSION,$MINOR_VERSION,$BUILD,$REVISION
  PRODUCTVERSION    $MAJOR_VERSION,$MINOR_VERSION,$BUILD,$REVISION
  FILEFLAGSMASK     VS_FFI_FILEFLAGSMASK
#ifdef _DEBUG
  FILEFLAGS         VS_FF_DEBUG | VS_FF_PRERELEASE
#else
  FILEFLAGS         0
#endif
  FILEOS            VOS_NT_WINDOWS32
  FILETYPE          $FILETYPE
  FILESUBTYPE       0
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904E4"
    BEGIN
      VALUE "CompanyName", "$COMPANY_NAME"
      VALUE "FileDescription", "$FILE_DESCRIPTION"
      VALUE "FileVersion", "$MAJOR_VERSION.$MINOR_VERSION from $BUILDDATE"
      VALUE "InternalName", "$INTERNAL_NAME"
      VALUE "LegalCopyright", "$COPYRIGHT"
      VALUE "OriginalFilename", "$ORIGINAL_FILE_NAME"
      VALUE "ProductName", "$PRODUCT_NAME"
      VALUE "ProductVersion", "$MAJOR_VERSION.$MINOR_VERSION from $BUILDDATE"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
EOF
