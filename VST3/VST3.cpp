/*----------------------------------------------------------------------------
 ChucK VST3 Chugin

 Allows loading and using VST3 plugins in ChucK.

 Copyright (c) 2025 CCRMA, Stanford University.  All rights reserved.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 U.S.A.
 -----------------------------------------------------------------------------*/

#include "chugin.h"
#include <stdio.h>
#include <string.h>
#include <vector>
#include <map>
#include <string>

// VST3 SDK includes
#ifdef HAVE_VST3_SDK
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/base/funknown.h"
#include "base/source/fstring.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
#endif

// Forward declarations
CK_DLL_CTOR(vst3_ctor);
CK_DLL_DTOR(vst3_dtor);
CK_DLL_TICK(vst3_tick);

CK_DLL_MFUN(vst3_load);
CK_DLL_MFUN(vst3_close);
CK_DLL_MFUN(vst3_list);
CK_DLL_MFUN(vst3_set_param);
CK_DLL_MFUN(vst3_set_param_by_name);
CK_DLL_MFUN(vst3_get_param);
CK_DLL_MFUN(vst3_get_param_by_name);
CK_DLL_MFUN(vst3_get_param_name);
CK_DLL_MFUN(vst3_get_param_count);
CK_DLL_MFUN(vst3_set_preset);
CK_DLL_MFUN(vst3_set_preset_by_name);
CK_DLL_MFUN(vst3_get_preset);
CK_DLL_MFUN(vst3_get_preset_count);
CK_DLL_MFUN(vst3_get_preset_name);
CK_DLL_MFUN(vst3_list_presets);
CK_DLL_MFUN(vst3_bypass);
CK_DLL_MFUN(vst3_send_midi);
CK_DLL_MFUN(vst3_note_on);
CK_DLL_MFUN(vst3_note_off);
CK_DLL_MFUN(vst3_control_change);
CK_DLL_MFUN(vst3_program_change);
CK_DLL_MFUN(vst3_is_instrument);

t_CKINT vst3_data_offset = 0;

#ifdef HAVE_VST3_SDK

class VST3Wrapper
{
public:
    VST3Wrapper(t_CKFLOAT sampleRate)
        : m_sampleRate(sampleRate)
        , m_component(nullptr)
        , m_controller(nullptr)
        , m_processor(nullptr)
        , m_bypass(false)
        , m_isInstrument(false)
        , m_processData()
        , m_processContext()
        , m_eventList()
        , m_inputEvents()
        , m_outputEvents()
    {
        // Initialize process data
        memset(&m_processData, 0, sizeof(m_processData));
        memset(&m_processContext, 0, sizeof(m_processContext));

        // Allocate audio buffers (mono input, stereo output for now)
        m_inputBuffer[0] = (Sample32*)calloc(1, sizeof(Sample32));
        m_outputBuffer[0] = (Sample32*)calloc(1, sizeof(Sample32));
        m_outputBuffer[1] = (Sample32*)calloc(1, sizeof(Sample32));

        // Setup audio bus buffers
        m_processData.numSamples = 1;
        m_processData.symbolicSampleSize = kSample32;

        m_inputs[0].channelBuffers32 = m_inputBuffer;
        m_inputs[0].numChannels = 1;
        m_inputs[0].silenceFlags = 0;

        m_outputs[0].channelBuffers32 = m_outputBuffer;
        m_outputs[0].numChannels = 2;
        m_outputs[0].silenceFlags = 0;

        m_processData.inputs = m_inputs;
        m_processData.outputs = m_outputs;
        m_processData.numInputs = 1;
        m_processData.numOutputs = 1;

        // Setup event lists
        m_processData.inputEvents = &m_inputEvents;
        m_processData.outputEvents = &m_outputEvents;

        // Setup process context
        m_processContext.sampleRate = m_sampleRate;
        m_processContext.state = ProcessContext::kPlaying;
        m_processData.processContext = &m_processContext;
    }

    ~VST3Wrapper()
    {
        close();
        if(m_inputBuffer[0]) free(m_inputBuffer[0]);
        if(m_outputBuffer[0]) free(m_outputBuffer[0]);
        if(m_outputBuffer[1]) free(m_outputBuffer[1]);
    }

    bool load(const char* path)
    {
        close();

        std::string errorDescription;
        auto module = VST3::Hosting::Module::create(path, errorDescription);

        if(!module)
        {
            fprintf(stderr, "[VST3]: Could not load module: %s\n", errorDescription.c_str());
            return false;
        }

        auto factory = module->getFactory();

        // Get the first audio module class
        for(auto& classInfo : factory.classInfos())
        {
            if(classInfo.category() == kVstAudioEffectClass)
            {
                m_component = factory.createInstance<IComponent>(classInfo.ID());
                break;
            }
        }

        if(!m_component)
        {
            fprintf(stderr, "[VST3]: Could not create component\n");
            return false;
        }

        // Initialize component with a simple host context
        FUnknownPtr<IHostApplication> hostApp(new HostApplication());
        if(m_component->initialize(hostApp) != kResultOk)
        {
            fprintf(stderr, "[VST3]: Could not initialize component\n");
            m_component->release();
            m_component = nullptr;
            return false;
        }

        // Get processor interface
        if(m_component->queryInterface(IAudioProcessor::iid, (void**)&m_processor) != kResultOk)
        {
            fprintf(stderr, "[VST3]: Could not get audio processor interface\n");
            m_component->terminate();
            m_component->release();
            m_component = nullptr;
            return false;
        }

        // Get edit controller
        TUID controllerCID;
        if(m_component->getControllerClassId(controllerCID) == kResultOk)
        {
            m_controller = factory.createInstance<IEditController>(controllerCID);
            if(m_controller)
            {
                m_controller->initialize(hostApp);
            }
        }

        if(!m_controller)
        {
            // Try to get controller from component
            if(m_component->queryInterface(IEditController::iid, (void**)&m_controller) != kResultOk)
            {
                fprintf(stderr, "[VST3]: Warning - no edit controller available\n");
            }
        }

        // Setup audio processing
        ProcessSetup setup;
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = 1;
        setup.sampleRate = m_sampleRate;

        if(m_processor->setupProcessing(setup) != kResultOk)
        {
            fprintf(stderr, "[VST3]: Could not setup processing\n");
            close();
            return false;
        }

        // Activate component
        if(m_component->setActive(true) != kResultOk)
        {
            fprintf(stderr, "[VST3]: Could not activate component\n");
            close();
            return false;
        }

        // Start processing
        if(m_processor->setProcessing(true) != kResultOk)
        {
            fprintf(stderr, "[VST3]: Could not start processing\n");
            close();
            return false;
        }

        // Check if it's an instrument by examining the first class info
        for(auto& classInfo : factory.classInfos())
        {
            if(classInfo.category() == kVstAudioEffectClass)
            {
                auto subcats = classInfo.subCategories();
                for(const auto& subcat : subcats)
                {
                    if(subcat.find("Instrument") != std::string::npos ||
                       subcat.find("Synth") != std::string::npos)
                    {
                        m_isInstrument = true;
                        break;
                    }
                }
                break;
            }
        }

        // Cache parameters
        cacheParameters();

        // Cache presets
        cachePresets();

        return true;
    }

    void close()
    {
        if(m_processor)
        {
            m_processor->setProcessing(false);
            m_processor->release();
            m_processor = nullptr;
        }

        if(m_controller)
        {
            // Only terminate if controller is separate from component
            FUnknownPtr<IEditController> compAsController(m_component);
            if(!compAsController || compAsController.getInterface() != m_controller)
            {
                m_controller->terminate();
                m_controller->release();
            }
            m_controller = nullptr;
        }

        if(m_component)
        {
            m_component->setActive(false);
            m_component->terminate();
            m_component->release();
            m_component = nullptr;
        }

        m_parameters.clear();
        m_presets.clear();
        m_isInstrument = false;
    }

    SAMPLE tick(SAMPLE input)
    {
        if(!m_processor || m_bypass)
            return input;

        // Set input
        m_inputBuffer[0][0] = input;

        // Clear output
        m_outputBuffer[0][0] = 0.0f;
        m_outputBuffer[1][0] = 0.0f;

        // Process
        tresult result = m_processor->process(m_processData);

        if(result != kResultOk)
        {
            static bool errorLogged = false;
            if(!errorLogged)
            {
                fprintf(stderr, "[VST3]: Processing error\n");
                errorLogged = true;
            }
            return m_isInstrument ? 0.0 : input;
        }

        // Clear event list for next process call
        m_inputEvents.clear();

        // Update process context
        m_processContext.projectTimeSamples += 1;

        // Return mono output (left channel)
        return m_outputBuffer[0][0];
    }

    bool setParameter(t_CKINT index, t_CKFLOAT value)
    {
        if(!m_controller || index < 0 || index >= m_parameters.size())
            return false;

        ParamID paramID = m_parameters[index].id;
        ParamValue normalized = m_controller->plainParamToNormalized(paramID, value);
        m_controller->setParamNormalized(paramID, normalized);

        return true;
    }

    bool setParameterByName(const char* name, t_CKFLOAT value)
    {
        if(!m_controller)
            return false;

        for(size_t i = 0; i < m_parameters.size(); i++)
        {
            if(m_parameters[i].title == name)
            {
                ParamID paramID = m_parameters[i].id;
                ParamValue normalized = m_controller->plainParamToNormalized(paramID, value);
                m_controller->setParamNormalized(paramID, normalized);
                return true;
            }
        }
        return false;
    }

    t_CKFLOAT getParameter(t_CKINT index)
    {
        if(!m_controller || index < 0 || index >= m_parameters.size())
            return 0.0;

        ParamID paramID = m_parameters[index].id;
        ParamValue normalized = m_controller->getParamNormalized(paramID);
        return m_controller->normalizedParamToPlain(paramID, normalized);
    }

    t_CKFLOAT getParameterByName(const char* name, bool* found = nullptr)
    {
        if(!m_controller)
        {
            if(found) *found = false;
            return 0.0;
        }

        for(size_t i = 0; i < m_parameters.size(); i++)
        {
            if(m_parameters[i].title == name)
            {
                ParamID paramID = m_parameters[i].id;
                ParamValue normalized = m_controller->getParamNormalized(paramID);
                if(found) *found = true;
                return m_controller->normalizedParamToPlain(paramID, normalized);
            }
        }
        if(found) *found = false;
        return 0.0;
    }

    const char* getParameterName(t_CKINT index)
    {
        if(index < 0 || index >= m_parameters.size())
            return "";
        return m_parameters[index].title.c_str();
    }

    t_CKINT getParameterCount()
    {
        return m_parameters.size();
    }

    void setBypass(bool bypass)
    {
        m_bypass = bypass;
    }

    // Preset methods
    t_CKINT getPresetCount() const
    {
        return m_presets.size();
    }

    const char* getPresetName(t_CKINT index) const
    {
        if(index < 0 || index >= m_presets.size())
            return "";
        return m_presets[index].name.c_str();
    }

    bool setPreset(t_CKINT index)
    {
        if(!m_controller || index < 0 || index >= m_presets.size())
            return false;

        IUnitInfo* unitInfo = nullptr;
        if(m_controller->queryInterface(IUnitInfo::iid, (void**)&unitInfo) == kResultOk)
        {
            if(unitInfo->selectUnit(kRootUnitId) == kResultOk)
            {
                if(unitInfo->setUnitProgramData(kRootUnitId, index, nullptr) == kResultOk)
                {
                    unitInfo->release();
                    return true;
                }
            }
            unitInfo->release();
        }
        return false;
    }

    bool setPresetByName(const char* name)
    {
        if(!m_controller)
            return false;

        for(size_t i = 0; i < m_presets.size(); i++)
        {
            if(m_presets[i].name == name)
            {
                return setPreset(i);
            }
        }
        return false;
    }

    t_CKINT getPreset() const
    {
        if(!m_controller)
            return -1;

        IUnitInfo* unitInfo = nullptr;
        if(m_controller->queryInterface(IUnitInfo::iid, (void**)&unitInfo) == kResultOk)
        {
            UnitID selectedUnit = unitInfo->getSelectedUnit();
            unitInfo->release();
            // Return the selected unit as preset index (simplified)
            return selectedUnit;
        }
        return -1;
    }

    void listPresets() const
    {
        if(m_presets.empty())
        {
            fprintf(stderr, "[VST3]: No presets available\n");
            return;
        }

        fprintf(stderr, "\n[VST3]: Available Presets:\n");
        fprintf(stderr, "----------------------------------------\n");
        for(size_t i = 0; i < m_presets.size(); i++)
        {
            fprintf(stderr, "  [%zu] %s\n", i, m_presets[i].name.c_str());
        }
        fprintf(stderr, "----------------------------------------\n");
        fprintf(stderr, "Total: %zu presets\n\n", m_presets.size());
    }

    // MIDI methods
    bool sendMIDI(t_CKINT status, t_CKINT data1, t_CKINT data2)
    {
        if(!m_processor || !m_isInstrument)
            return false;

        Event event;
        memset(&event, 0, sizeof(Event));
        event.busIndex = 0;
        event.sampleOffset = 0;

        uint8_t channel = status & 0x0F;
        uint8_t messageType = status & 0xF0;

        switch(messageType)
        {
            case 0x90: // Note On
                event.type = Event::kNoteOnEvent;
                event.noteOn.channel = channel;
                event.noteOn.pitch = data1;
                event.noteOn.velocity = data2 / 127.0f;
                event.noteOn.noteId = -1;
                break;

            case 0x80: // Note Off
                event.type = Event::kNoteOffEvent;
                event.noteOff.channel = channel;
                event.noteOff.pitch = data1;
                event.noteOff.velocity = data2 / 127.0f;
                event.noteOff.noteId = -1;
                break;

            case 0xB0: // Control Change
                {
                    CtrlNumber ctrlNumber = data1;
                    ParamValue value = data2 / 127.0;

                    // Map MIDI CC to VST3 parameter if possible
                    IMidiMapping* midiMapping = nullptr;
                    if(m_controller->queryInterface(IMidiMapping::iid, (void**)&midiMapping) == kResultOk)
                    {
                        ParamID paramID;
                        if(midiMapping->getMidiControllerAssignment(0, channel, ctrlNumber, paramID) == kResultOk)
                        {
                            m_controller->setParamNormalized(paramID, value);
                        }
                        midiMapping->release();
                    }
                    return true;
                }

            case 0xC0: // Program Change
                setPreset(data1);
                return true;

            default:
                return false;
        }

        m_inputEvents.addEvent(event);
        return true;
    }

    bool noteOn(t_CKINT pitch, t_CKINT velocity)
    {
        return sendMIDI(0x90, pitch, velocity);
    }

    bool noteOff(t_CKINT pitch)
    {
        return sendMIDI(0x80, pitch, 0);
    }

    bool controlChange(t_CKINT cc, t_CKINT value)
    {
        return sendMIDI(0xB0, cc, value);
    }

    bool programChange(t_CKINT program)
    {
        return sendMIDI(0xC0, program, 0);
    }

    bool isInstrument() const
    {
        return m_isInstrument;
    }

    static void listVST3Plugins()
    {
        // Common VST3 plugin paths
        std::vector<std::string> searchPaths;

#ifdef __APPLE__
        searchPaths.push_back("/Library/Audio/Plug-Ins/VST3");
        searchPaths.push_back("~/Library/Audio/Plug-Ins/VST3");
#elif defined(_WIN32)
        searchPaths.push_back("C:\\Program Files\\Common Files\\VST3");
        searchPaths.push_back("C:\\Program Files (x86)\\Common Files\\VST3");
#else // Linux
        searchPaths.push_back("/usr/lib/vst3");
        searchPaths.push_back("/usr/local/lib/vst3");
        searchPaths.push_back("~/.vst3");
#endif

        fprintf(stderr, "\n[VST3]: Available VST3 Plugins:\n");
        fprintf(stderr, "----------------------------------------\n");

        int count = 0;
        for(const auto& path : searchPaths)
        {
            // TODO: Scan directories and list plugins
            fprintf(stderr, "Scanning: %s\n", path.c_str());
        }

        fprintf(stderr, "----------------------------------------\n");
        fprintf(stderr, "Total: %d VST3 plugins\n\n", count);
    }

private:
    struct ParameterInfo
    {
        ParamID id;
        std::string title;
    };

    struct PresetInfo
    {
        int32 index;
        std::string name;
    };

    void cacheParameters()
    {
        m_parameters.clear();

        if(!m_controller)
            return;

        int32 paramCount = m_controller->getParameterCount();
        for(int32 i = 0; i < paramCount; i++)
        {
            ParameterInfo paramInfo;
            Vst::ParameterInfo info;

            if(m_controller->getParameterInfo(i, info) == kResultOk)
            {
                paramInfo.id = info.id;

                // Convert UTF-16 title to UTF-8
                String str(info.title);
                str.toMultiByte(kCP_Utf8);
                paramInfo.title = str.text8();

                m_parameters.push_back(paramInfo);
            }
        }
    }

    void cachePresets()
    {
        m_presets.clear();

        if(!m_controller)
            return;

        IUnitInfo* unitInfo = nullptr;
        if(m_controller->queryInterface(IUnitInfo::iid, (void**)&unitInfo) == kResultOk)
        {
            int32 programListCount = unitInfo->getProgramListCount();

            if(programListCount > 0)
            {
                ProgramListInfo listInfo;
                if(unitInfo->getProgramListInfo(0, listInfo) == kResultOk)
                {
                    for(int32 i = 0; i < listInfo.programCount; i++)
                    {
                        PresetInfo preset;
                        preset.index = i;

                        String128 name;
                        if(unitInfo->getProgramName(0, i, name) == kResultOk)
                        {
                            String str(name);
                            str.toMultiByte(kCP_Utf8);
                            preset.name = str.text8();
                        }
                        else
                        {
                            char buf[32];
                            snprintf(buf, sizeof(buf), "Preset %d", i);
                            preset.name = buf;
                        }

                        m_presets.push_back(preset);
                    }
                }
            }
            unitInfo->release();
        }
    }

    // Simple event list implementation
    class EventList : public IEventList
    {
    public:
        EventList() {}

        DECLARE_FUNKNOWN_METHODS

        int32 PLUGIN_API getEventCount() override { return (int32)events.size(); }

        tresult PLUGIN_API getEvent(int32 index, Event& e) override
        {
            if(index >= 0 && index < (int32)events.size())
            {
                e = events[index];
                return kResultOk;
            }
            return kResultFalse;
        }

        tresult PLUGIN_API addEvent(Event& e) override
        {
            events.push_back(e);
            return kResultOk;
        }

        void clear() { events.clear(); }

    private:
        std::vector<Event> events;
    };

    IComponent* m_component;
    IEditController* m_controller;
    IAudioProcessor* m_processor;

    t_CKFLOAT m_sampleRate;
    bool m_bypass;
    bool m_isInstrument;

    // Audio buffers
    Sample32* m_inputBuffer[1];
    Sample32* m_outputBuffer[2];
    AudioBusBuffers m_inputs[1];
    AudioBusBuffers m_outputs[1];

    // Process data
    ProcessData m_processData;
    ProcessContext m_processContext;
    EventList m_eventList;
    EventList m_inputEvents;
    EventList m_outputEvents;

    std::vector<ParameterInfo> m_parameters;
    std::vector<PresetInfo> m_presets;
};

// Implement EventList FUnknown methods
IMPLEMENT_FUNKNOWN_METHODS(VST3Wrapper::EventList, IEventList, IEventList::iid)

#else // !HAVE_VST3_SDK

// Stub implementation when VST3 SDK is not available
class VST3Wrapper
{
public:
    VST3Wrapper(t_CKFLOAT sampleRate) {}
    ~VST3Wrapper() {}
    bool load(const char* path) { return false; }
    void close() {}
    SAMPLE tick(SAMPLE input) { return input; }
    bool setParameter(t_CKINT index, t_CKFLOAT value) { return false; }
    bool setParameterByName(const char* name, t_CKFLOAT value) { return false; }
    t_CKFLOAT getParameter(t_CKINT index) { return 0.0; }
    t_CKFLOAT getParameterByName(const char* name, bool* found = nullptr) { if(found) *found = false; return 0.0; }
    const char* getParameterName(t_CKINT index) { return ""; }
    t_CKINT getParameterCount() { return 0; }
    void setBypass(bool bypass) {}
    t_CKINT getPresetCount() const { return 0; }
    const char* getPresetName(t_CKINT index) const { return ""; }
    bool setPreset(t_CKINT index) { return false; }
    bool setPresetByName(const char* name) { return false; }
    t_CKINT getPreset() const { return -1; }
    void listPresets() const { fprintf(stderr, "[VST3]: VST3 SDK not available\n"); }
    bool sendMIDI(t_CKINT status, t_CKINT data1, t_CKINT data2) { return false; }
    bool noteOn(t_CKINT pitch, t_CKINT velocity) { return false; }
    bool noteOff(t_CKINT pitch) { return false; }
    bool controlChange(t_CKINT cc, t_CKINT value) { return false; }
    bool programChange(t_CKINT program) { return false; }
    bool isInstrument() const { return false; }
    static void listVST3Plugins() {
        fprintf(stderr, "[VST3]: VST3 SDK not available. Please build with VST3 SDK.\n");
    }
};

#endif // HAVE_VST3_SDK

// ChucK DLL Query
CK_DLL_QUERY(VST3)
{
    QUERY->setname(QUERY, "VST3");

    QUERY->begin_class(QUERY, "VST3", "UGen");
    QUERY->doc_class(QUERY, "Load and use VST3 plugins in ChucK. "
                            "VST3 plugins can be effects or instruments. "
                            "Requires VST3 SDK to build.");
    QUERY->add_ex(QUERY, "effects/VST3.ck");

    QUERY->add_ctor(QUERY, vst3_ctor);
    QUERY->add_dtor(QUERY, vst3_dtor);

    QUERY->add_ugen_func(QUERY, vst3_tick, NULL, 1, 1);

    QUERY->add_mfun(QUERY, vst3_load, "int", "load");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->doc_func(QUERY, "Load a VST3 plugin by file path. "
                          "Returns 1 on success, 0 on failure.");

    QUERY->add_mfun(QUERY, vst3_close, "void", "close");
    QUERY->doc_func(QUERY, "Close the currently loaded VST3 plugin.");

    QUERY->add_mfun(QUERY, vst3_list, "void", "list");
    QUERY->doc_func(QUERY, "List all available VST3 plugins on the system.");

    QUERY->add_mfun(QUERY, vst3_set_param, "void", "setParam");
    QUERY->add_arg(QUERY, "int", "index");
    QUERY->add_arg(QUERY, "float", "value");
    QUERY->doc_func(QUERY, "Set a parameter value by index.");

    QUERY->add_mfun(QUERY, vst3_set_param_by_name, "int", "setParamByName");
    QUERY->add_arg(QUERY, "string", "name");
    QUERY->add_arg(QUERY, "float", "value");
    QUERY->doc_func(QUERY, "Set a parameter value by name. Returns 1 on success, 0 if parameter not found.");

    QUERY->add_mfun(QUERY, vst3_get_param, "float", "getParam");
    QUERY->add_arg(QUERY, "int", "index");
    QUERY->doc_func(QUERY, "Get a parameter value by index.");

    QUERY->add_mfun(QUERY, vst3_get_param_by_name, "float", "getParamByName");
    QUERY->add_arg(QUERY, "string", "name");
    QUERY->doc_func(QUERY, "Get a parameter value by name. Returns 0.0 if parameter not found.");

    QUERY->add_mfun(QUERY, vst3_get_param_name, "string", "paramName");
    QUERY->add_arg(QUERY, "int", "index");
    QUERY->doc_func(QUERY, "Get a parameter name by index.");

    QUERY->add_mfun(QUERY, vst3_get_param_count, "int", "paramCount");
    QUERY->doc_func(QUERY, "Get the number of parameters available.");

    QUERY->add_mfun(QUERY, vst3_set_preset, "int", "setPreset");
    QUERY->add_arg(QUERY, "int", "index");
    QUERY->doc_func(QUERY, "Set the current preset by index. Returns 1 on success, 0 on failure.");

    QUERY->add_mfun(QUERY, vst3_set_preset_by_name, "int", "setPresetByName");
    QUERY->add_arg(QUERY, "string", "name");
    QUERY->doc_func(QUERY, "Set the current preset by name. Returns 1 on success, 0 if preset not found.");

    QUERY->add_mfun(QUERY, vst3_get_preset, "int", "getPreset");
    QUERY->doc_func(QUERY, "Get the current preset index. Returns -1 if no preset is active.");

    QUERY->add_mfun(QUERY, vst3_get_preset_count, "int", "presetCount");
    QUERY->doc_func(QUERY, "Get the number of presets available.");

    QUERY->add_mfun(QUERY, vst3_get_preset_name, "string", "presetName");
    QUERY->add_arg(QUERY, "int", "index");
    QUERY->doc_func(QUERY, "Get a preset name by index.");

    QUERY->add_mfun(QUERY, vst3_list_presets, "void", "listPresets");
    QUERY->doc_func(QUERY, "List all available presets to the console.");

    QUERY->add_mfun(QUERY, vst3_bypass, "void", "bypass");
    QUERY->add_arg(QUERY, "int", "bypass");
    QUERY->doc_func(QUERY, "Bypass the VST3 plugin (1 = bypass, 0 = active).");

    // MIDI methods
    QUERY->add_mfun(QUERY, vst3_send_midi, "int", "sendMIDI");
    QUERY->add_arg(QUERY, "int", "status");
    QUERY->add_arg(QUERY, "int", "data1");
    QUERY->add_arg(QUERY, "int", "data2");
    QUERY->doc_func(QUERY, "Send raw MIDI message to VST3 plugin (for instrument types). Returns 1 on success.");

    QUERY->add_mfun(QUERY, vst3_note_on, "int", "noteOn");
    QUERY->add_arg(QUERY, "int", "pitch");
    QUERY->add_arg(QUERY, "int", "velocity");
    QUERY->doc_func(QUERY, "Send MIDI note-on message (channel 0). Returns 1 on success.");

    QUERY->add_mfun(QUERY, vst3_note_off, "int", "noteOff");
    QUERY->add_arg(QUERY, "int", "pitch");
    QUERY->doc_func(QUERY, "Send MIDI note-off message (channel 0). Returns 1 on success.");

    QUERY->add_mfun(QUERY, vst3_control_change, "int", "controlChange");
    QUERY->add_arg(QUERY, "int", "cc");
    QUERY->add_arg(QUERY, "int", "value");
    QUERY->doc_func(QUERY, "Send MIDI control change message (channel 0). Returns 1 on success.");

    QUERY->add_mfun(QUERY, vst3_program_change, "int", "programChange");
    QUERY->add_arg(QUERY, "int", "program");
    QUERY->doc_func(QUERY, "Send MIDI program change message (channel 0). Returns 1 on success.");

    QUERY->add_mfun(QUERY, vst3_is_instrument, "int", "isInstrument");
    QUERY->doc_func(QUERY, "Check if loaded VST3 plugin is an instrument. Returns 1 if true.");

    vst3_data_offset = QUERY->add_mvar(QUERY, "int", "@vst3_data", false);

    QUERY->end_class(QUERY);

    return TRUE;
}

// Implementation
CK_DLL_CTOR(vst3_ctor)
{
    OBJ_MEMBER_INT(SELF, vst3_data_offset) = 0;

    VST3Wrapper* wrapper = new VST3Wrapper(API->vm->srate(VM));

    OBJ_MEMBER_INT(SELF, vst3_data_offset) = (t_CKINT)wrapper;
}

CK_DLL_DTOR(vst3_dtor)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    if(wrapper)
    {
        delete wrapper;
        OBJ_MEMBER_INT(SELF, vst3_data_offset) = 0;
    }
}

CK_DLL_TICK(vst3_tick)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);

    if(wrapper)
        *out = wrapper->tick(in);
    else
        *out = in;

    return TRUE;
}

CK_DLL_MFUN(vst3_load)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    std::string path = GET_NEXT_STRING_SAFE(ARGS);

    RETURN->v_int = wrapper ? wrapper->load(path.c_str()) : 0;
}

CK_DLL_MFUN(vst3_close)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    if(wrapper)
        wrapper->close();
}

CK_DLL_MFUN(vst3_list)
{
    VST3Wrapper::listVST3Plugins();
}

CK_DLL_MFUN(vst3_set_param)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    t_CKINT index = GET_NEXT_INT(ARGS);
    t_CKFLOAT value = GET_NEXT_FLOAT(ARGS);

    if(wrapper)
        wrapper->setParameter(index, value);
}

CK_DLL_MFUN(vst3_set_param_by_name)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    std::string name = GET_NEXT_STRING_SAFE(ARGS);
    t_CKFLOAT value = GET_NEXT_FLOAT(ARGS);

    RETURN->v_int = (wrapper && wrapper->setParameterByName(name.c_str(), value)) ? 1 : 0;
}

CK_DLL_MFUN(vst3_get_param)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    t_CKINT index = GET_NEXT_INT(ARGS);

    RETURN->v_float = wrapper ? wrapper->getParameter(index) : 0.0;
}

CK_DLL_MFUN(vst3_get_param_by_name)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    std::string name = GET_NEXT_STRING_SAFE(ARGS);

    RETURN->v_float = wrapper ? wrapper->getParameterByName(name.c_str()) : 0.0;
}

CK_DLL_MFUN(vst3_get_param_name)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    t_CKINT index = GET_NEXT_INT(ARGS);

    const char* name = wrapper ? wrapper->getParameterName(index) : "";
    RETURN->v_string = API->object->create_string(VM, name, false);
}

CK_DLL_MFUN(vst3_get_param_count)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    RETURN->v_int = wrapper ? wrapper->getParameterCount() : 0;
}

CK_DLL_MFUN(vst3_set_preset)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    t_CKINT index = GET_NEXT_INT(ARGS);

    RETURN->v_int = (wrapper && wrapper->setPreset(index)) ? 1 : 0;
}

CK_DLL_MFUN(vst3_set_preset_by_name)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    std::string name = GET_NEXT_STRING_SAFE(ARGS);

    RETURN->v_int = (wrapper && wrapper->setPresetByName(name.c_str())) ? 1 : 0;
}

CK_DLL_MFUN(vst3_get_preset)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    RETURN->v_int = wrapper ? wrapper->getPreset() : -1;
}

CK_DLL_MFUN(vst3_get_preset_count)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    RETURN->v_int = wrapper ? wrapper->getPresetCount() : 0;
}

CK_DLL_MFUN(vst3_get_preset_name)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    t_CKINT index = GET_NEXT_INT(ARGS);

    const char* name = wrapper ? wrapper->getPresetName(index) : "";
    RETURN->v_string = API->object->create_string(VM, name, false);
}

CK_DLL_MFUN(vst3_list_presets)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    if(wrapper)
        wrapper->listPresets();
}

CK_DLL_MFUN(vst3_bypass)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    t_CKINT bypass = GET_NEXT_INT(ARGS);

    if(wrapper)
        wrapper->setBypass(bypass != 0);
}

CK_DLL_MFUN(vst3_send_midi)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    t_CKINT status = GET_NEXT_INT(ARGS);
    t_CKINT data1 = GET_NEXT_INT(ARGS);
    t_CKINT data2 = GET_NEXT_INT(ARGS);

    RETURN->v_int = (wrapper && wrapper->sendMIDI(status, data1, data2)) ? 1 : 0;
}

CK_DLL_MFUN(vst3_note_on)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    t_CKINT pitch = GET_NEXT_INT(ARGS);
    t_CKINT velocity = GET_NEXT_INT(ARGS);

    RETURN->v_int = (wrapper && wrapper->noteOn(pitch, velocity)) ? 1 : 0;
}

CK_DLL_MFUN(vst3_note_off)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    t_CKINT pitch = GET_NEXT_INT(ARGS);

    RETURN->v_int = (wrapper && wrapper->noteOff(pitch)) ? 1 : 0;
}

CK_DLL_MFUN(vst3_control_change)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    t_CKINT cc = GET_NEXT_INT(ARGS);
    t_CKINT value = GET_NEXT_INT(ARGS);

    RETURN->v_int = (wrapper && wrapper->controlChange(cc, value)) ? 1 : 0;
}

CK_DLL_MFUN(vst3_program_change)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    t_CKINT program = GET_NEXT_INT(ARGS);

    RETURN->v_int = (wrapper && wrapper->programChange(program)) ? 1 : 0;
}

CK_DLL_MFUN(vst3_is_instrument)
{
    VST3Wrapper* wrapper = (VST3Wrapper*)OBJ_MEMBER_INT(SELF, vst3_data_offset);
    RETURN->v_int = (wrapper && wrapper->isInstrument()) ? 1 : 0;
}
