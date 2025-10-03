# vst3.chug

Towards a vst3 chugin

If you want to work on fixing the VST3 linking issues in the future, the solution would be to
  rebuild the VST3 SDK as universal binaries:

```sh
cd vst3.chug/vst3sdk/build
cmake .. -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"
make
```

