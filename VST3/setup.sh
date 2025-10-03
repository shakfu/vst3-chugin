 
if [ ! -d "vst3sdk" ]; then
	git clone --depth=1 --recursive https://github.com/steinbergmedia/vst3sdk
fi && \
	cd vst3sdk && mkdir build && cd build && \
	cmake .. -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" && \
	cmake --build . --config Release
