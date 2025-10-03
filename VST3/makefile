# chugin name
CHUGIN_NAME=VST3

# all of the c/cpp files that compose this chugin
C_MODULES=
CXX_MODULES=VST3.cpp

# where to find chugin.h
CK_SRC_PATH?=../chuck/include/

# VST3 SDK path - can be overridden via environment variable
VST3_SDK_PATH?=$(PWD)/vst3sdk

# ---------------------------------------------------------------------------- #
# you won't generally need to change anything below this line for a new chugin #
# ---------------------------------------------------------------------------- #

# default target: print usage message and quit
current:
	@echo "[chugin build]: please use one of the following configurations:"
	@echo "   make linux, make mac, make web, or make win32"

ifneq ($(CK_TARGET),)
.DEFAULT_GOAL:=$(CK_TARGET)
ifeq ($(MAKECMDGOALS),)
MAKECMDGOALS:=$(.DEFAULT_GOAL)
endif
endif

.PHONY: mac osx linux linux-oss linux-jack linux-alsa win32
mac osx linux linux-oss linux-jack linux-alsa: all

win32:
	make -f makefile.win

CC=gcc
CXX=gcc
LD=g++

CHUGIN_PATH=/usr/local/lib/chuck

ifneq (,$(strip $(filter mac osx bin-dist-osx,$(MAKECMDGOALS))))
include makefile.mac
endif

ifneq (,$(strip $(filter linux,$(MAKECMDGOALS))))
include makefile.linux
endif

ifneq (,$(strip $(filter linux-oss,$(MAKECMDGOALS))))
include makefile.linux
endif

ifneq (,$(strip $(filter linux-jack,$(MAKECMDGOALS))))
include makefile.linux
endif

ifneq (,$(strip $(filter linux-alsa,$(MAKECMDGOALS))))
include makefile.linux
endif

# Check if VST3 SDK is available
ifneq ("$(wildcard $(VST3_SDK_PATH))","")
FLAGS+= -DHAVE_VST3_SDK -I$(VST3_SDK_PATH)
else
$(warning VST3 SDK not found at $(VST3_SDK_PATH). Building stub version.)
$(warning To build with VST3 support, install the VST3 SDK and set VST3_SDK_PATH)
endif

ifneq ($(CHUCK_DEBUG),)
FLAGS+= -g
else
FLAGS+= -O3
endif

ifneq ($(CHUCK_STRICT),)
FLAGS+= -Werror
endif

# default: build a dynamic chugin
CK_CHUGIN_STATIC?=0

ifeq ($(CK_CHUGIN_STATIC),0)
SUFFIX=.chug
else
SUFFIX=.schug
FLAGS+= -D__CK_DLL_STATIC__
endif
# webchugin extension
WEBSUFFIX=.wasm

C_OBJECTS=$(addsuffix .o,$(basename $(C_MODULES)))
CXX_OBJECTS=$(addsuffix .o,$(basename $(CXX_MODULES)))

CHUG=$(addsuffix $(SUFFIX),$(CHUGIN_NAME))
WEBCHUG=$(addsuffix $(WEBSUFFIX),$(CHUG))

all: $(CHUG)

$(CHUG): $(C_OBJECTS) $(CXX_OBJECTS) $(VST3_SDK_OBJECTS)
ifeq ($(CK_CHUGIN_STATIC),0)
	$(LD) $(LDFLAGS) -o $@ $^
else
	ar rv $@ $^
	ranlib $@
endif

# Compile VST3 SDK sources
$(VST3_SDK_OBJECTS): %.o: %.cpp
	$(CXX) $(FLAGS) -c -o $@ $<

$(C_OBJECTS): %.o: %.c
	$(CC) $(FLAGS) -c -o $@ $<

$(CXX_OBJECTS): %.o: %.cpp $(CK_SRC_PATH)/chugin.h
	$(CXX) $(FLAGS) -c -o $@ $<

# build as webchugin
web:
	emcc -O3 -s SIDE_MODULE=1 -s DISABLE_EXCEPTION_CATCHING=0 -fPIC -Wformat=0 \
	-I ../chuck/include/ $(CXX_MODULES) $(C_MODULES) -o $(WEBCHUG)

install: $(CHUG)
	mkdir -p $(CHUGIN_PATH)
	cp $^ $(CHUGIN_PATH)
	chmod 755 $(CHUGIN_PATH)/$(CHUG)

clean:
	rm -rf $(C_OBJECTS) $(CXX_OBJECTS) $(VST3_SDK_OBJECTS) $(CHUG) $(WEBCHUG) Release Debug
