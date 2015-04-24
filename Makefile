CCx86        := i686-w64-mingw32-gcc
CXXx86       := i686-w64-mingw32-g++
WINDRESx86   := i686-w64-mingw32-windres
WIDL         := i686-w64-mingw32-widl
CCamd64      := x86_64-w64-mingw32-gcc
CXXamd64     := x86_64-w64-mingw32-g++
WINDRESamd64 := x86_64-w64-mingw32-windres


FLAGS_debug   := -g -O0 -D_DEBUG
FLAGS_release := -O2 -DNDEBUG

CFLAGS_COMMON := -municode -DUNICODE -D_UNICODE -std=gnu99 -Id3d-headers -Iminhook/include
CXXFLAGS      := -municode -DUNICODE -D_UNICODE -std=c++11 -Id3d-headers -Iminhook/include -Wall -Wextra -fno-exceptions -fno-rtti
CFLAGS_3RDPARTY := $(CFLAGS_COMMON) -w
CFLAGS := $(CFLAGS_COMMON) -Wall -Wextra -Wno-format
LDFLAGS := -static -Wl,--enable-stdcall-fixup
LIBS    := -lgdi32 -luser32 -lole32

SILENT = @

all: out/dirs.stamp

define ALL_helper
  all: $(foreach target,$1,out/amd64/debug/$(target) out/amd64/release/$(target) out/x86/release/$(target) out/x86/debug/$(target))
endef

$(eval $(call ALL_helper,dd4seven-api.dll dd4seven-dwm.dll test-dx11.exe))

out/dirs.stamp:
	$(SILENT)for combo in amd64/release amd64/debug x86/release x86/debug; do \
	  mkdir -p out/$$combo/src; \
	  mkdir -p out/$$combo/minhook/src/hde; \
	done;
	$(SILENT)touch out/dirs.stamp

######
# Create a general target for .cpp and .c files
######
define CXX_target
  #$1: amd64/x86 $2: debug/release
  out/$1/$2/src/%.cpp.o out/$1/$2/src/%.cpp.d: src/%.cpp out/dirs.stamp
	$(SILENT)echo "CXX($1,$2)" $$<
	$(SILENT)$$(CXX$1) $(CXXFLAGS) $$(FLAGS_$2) -MMD -MF "out/$1/$2/$$<.d" -MT "out/$1/$2/$$<.o" -MP -c -o "out/$1/$2/$$<.o" "$$<"

  out/$1/$2/src/%.c.o out/$1/$2/src/%.c.d: src/%.c out/dirs.stamp
	$(SILENT)echo "CC($1,$2)" $$<
	$(SILENT)$$(CC$1) $(CFLAGS) $$(FLAGS_$2) -MMD -MF "out/$1/$2/$$<.d" -MT "out/$1/$2/$$<.o" -MP -c -o "out/$1/$2/$$<.o" "$$<"

  out/$1/$2/minhook/%.c.o: minhook/%.c out/dirs.stamp
	$(SILENT)echo "CC($1,$2)" $$<
	$(SILENT)$$(CC$1) $(CFLAGS_3RDPARTY) $$(FLAGS_$2) -c -o "out/$1/$2/$$<.o" "$$<"
endef

$(eval $(call CXX_target,amd64,release))
$(eval $(call CXX_target,amd64,debug))
$(eval $(call CXX_target,x86,release))
$(eval $(call CXX_target,x86,debug))

######
# Define macros for generating DLL targets
######
define DLL_target_helper
  #$1: some_name.dll $2: src/some.cpp src/other.cpp $3: arch $4: debug
  out/$3/$4/$1: $(patsubst %.cpp,out/$3/$4/%.cpp.o,$(patsubst %.c,out/$3/$4/%.c.o,$2))
	$(SILENT)/bin/sh src/generate-version-resource.sh "$1" > out/$3/$4/src/$1.rc
	$(SILENT)echo "WINDRES($3,$4)" out/$3/$4/src/$1.rc
	$(SILENT)$(WINDRES$3) --codepage=65001 -o out/$3/$4/src/$1.rc.o out/$3/$4/src/$1.rc
	$(SILENT)echo "LD($3,$4)" $$@
	$(SILENT)$(CXX$3) $$(FLAGS_$4) $(LDFLAGS) -shared -o "$$@" $$^ out/$3/$4/src/$1.rc.o $(LIBS)
endef
define EXE_target_helper
  #$1: some_name.exe $2: src/some.cpp src/other.cpp $3: arch $4: debug
  out/$3/$4/$1: $(patsubst %.cpp,out/$3/$4/%.cpp.o,$(patsubst %.c,out/$3/$4/%.c.o,$2))
	$(SILENT)/bin/sh src/generate-version-resource.sh "$1" > out/$3/$4/src/$1.rc
	$(SILENT)echo "WINDRES($3,$4)" out/$3/$4/src/$1.rc.o
	$(SILENT)$(WINDRES$3) --codepage=65001 -o out/$3/$4/src/$1.rc.o out/$3/$4/src/$1.rc
	$(SILENT)echo "LD($3,$4)" $$@
	$(SILENT)$(CXX$3) $$(FLAGS_$4) $(LDFLAGS) -o "$$@" $$^ out/$3/$4/src/$1.rc.o $(LIBS)
endef

define DLL_target
  $(call DLL_target_helper,$1,$2,amd64,release)
  $(call DLL_target_helper,$1,$2,amd64,debug)
  $(call DLL_target_helper,$1,$2,x86,release)
  $(call DLL_target_helper,$1,$2,x86,debug)
endef

define EXE_target
  $(call EXE_target_helper,$1,$2,amd64,release)
  $(call EXE_target_helper,$1,$2,amd64,debug)
  $(call EXE_target_helper,$1,$2,x86,release)
  $(call EXE_target_helper,$1,$2,x86,debug)
endef

#####
# Define our DLL targets
#####
$(eval $(call DLL_target,dd4seven-api.dll, \
    src/dd4seven-api.def \
    src/dd4seven-api.cpp \
    src/logger.cpp \
))
$(eval $(call DLL_target,dd4seven-dwm.dll, \
    src/dd4seven-dwm.cpp \
    src/logger.cpp \
    $(shell find minhook -name '*.c') \
))
$(eval $(call EXE_target,test-dx11.exe, \
    src/test-dx11.cpp \
    src/logger.cpp \
))

#####
# D3D header targets
#####
d3d-headers/%.h: d3d-headers/%.idl
	$(SILENT)echo WIDL $<
	$(SILENT)$(WIDL) -h "$<" -o "$@"

ifneq ($(MAKECMDGOALS),clean)
-include $(shell find . -name '*.d')
endif

clean:
	find out -name '*.o' -exec rm {} \;
	find out -name '*.d' -exec rm {} \;
	find out -name '*.dll' -exec rm {} \;
	find out -name 'text-dx11.exe' -exec rm {} \;
