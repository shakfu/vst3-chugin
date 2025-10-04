# Fixes Summary

## Critical Hosting Corrections
- **Persist module handle** – `VST3/VST3.cpp` now caches `VST3::Hosting::Module::Ptr` across the wrapper lifetime, preventing the plugin DLL from unloading once `load()` returns.
- **Robust bus negotiation** – `configureBuses()` inspects `IComponent`/`IAudioProcessor` bus metadata, negotiates speaker arrangements, and explicitly activates only the primary audio buses (`VST3/VST3.cpp`).
- **Dynamic buffer management** – Swapped fixed mono/stereo arrays for vectors sized from the negotiated channel counts, keeping `ProcessData` inputs/outputs consistent with the plugin layout.
- **Processing state initialization** – The wrapper seeds `ProcessSetup`/`ProcessContext` fields up front and refreshes them per block so `setupProcessing()` operates on valid data.

## Runtime Adjustments
- `tick()` populates every negotiated input channel, clears silence flags, and returns the processed sample from the plugin instead of a hard-coded buffer slot.
- `close()` now tears down audio buffers, resets `ProcessData`, and clears module/shared pointers to guarantee a clean reload path.

## Remaining Gaps
- Parameter automation still bypasses `IComponentHandler` and parameter-change queues.
- Multiple bus activation, block-based processing, and component/controller connection wiring remain TODOs for full VST3 host compliance.

