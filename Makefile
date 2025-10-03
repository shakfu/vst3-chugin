.PHONY: all setup mac linux win32 web clean

define build-vst
@$(MAKE) -C VST3 $1
endef

all: help

setup:
	@cd VST3 && sh setup.sh

mac:
	@$(call build-vst,"$@")
	@mv VST3/VST3.chug .

linux:
	@$(call build-vst,"$@")
	@mv VST3/VST3.chug .

web:
	@$(call build-vst,"$@")
	@mv VST3/VST3.webchug .

win32:
	@$(call build-vst,"$@")
	@mv VST3/VST3.chug .

clean:
	@$(call build-vst,"$@")
	@rm VST3.chug

help:
	@echo "please use one of the following configurations:"
	@echo "   make linux, make mac, make web, or make win32"
