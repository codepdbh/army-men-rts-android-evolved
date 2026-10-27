// Miles Sound System 6.1c digital API over the host AudioMixer and AAudio.
// Handles are small guest allocations so the game receives valid pointers.
std::unique_ptr<AudioMixer> audio;
uint32_t audioDriver=0;
std::vector<uint32_t> audioSamples;
// The game's own master volume (a copied PC profile may hold a near-silent
// value); the port setting replaces it unless set to follow the game.
int32_t gameMasterVolume=127,appliedMasterVolume=-1;
constexpr uint32_t audioSampleLimit=64;
void resetAudio(){audio.reset();audioDriver=0;audioSamples.clear();gameMasterVolume=127;appliedMasterVolume=-1;}
void pauseGuestAudio(bool paused){if(audio)audio->setPaused(paused);}
void applyMasterVolume(){
    if(!audio)return;int32_t port=runControl?runControl->masterVolume.load():-1,volume=port>=0?port:gameMasterVolume;
    if(volume!=appliedMasterVolume){audio->setMasterVolume(volume);appliedMasterVolume=volume;
        __android_log_print(ANDROID_LOG_INFO,"AMRTS","Master volume %d/127 (game %d/127)",volume,gameMasterVolume);}
}
bool audioSample(uint32_t handle){return audio&&std::find(audioSamples.begin(),audioSamples.end(),handle)!=audioSamples.end();}
uint32_t audioText(const char* text){
    size_t size=std::strlen(text)+1;uint32_t output=allocate(uint32_t(size));
    std::memcpy(kernelPtr(output,size,true),text,size);return output;
}
// `adb shell setprop debug.amrts.audio 1` traces every Miles call; `2` also
// captures 60 s of the final mix to armymenrts/audio_dump.f32 (float stereo).
const int audioDebug=[]{char value[PROP_VALUE_MAX]{};__system_property_get("debug.amrts.audio",value);return std::atoi(value);}();
bool audioDispatch(const std::string& name){
    if(name.compare(0,5,"_AIL_"))return false;
    if(audioDebug&&name!="_AIL_ms_count@0"&&name!="_AIL_sample_status@4"&&name!="_AIL_sample_position@4")
        __android_log_print(ANDROID_LOG_INFO,"AMRTS","Miles %s(%08x,%08x,%08x,%08x,%08x)",name.c_str(),arg(0),arg(1),arg(2),arg(3),arg(4));
    if(name=="_AIL_set_redist_directory@4"){ret(0,1);return true;}
    if(name=="_AIL_startup@0"){ret(1,0);return true;}
    if(name=="_AIL_shutdown@0"){resetAudio();ret(0,0);return true;}
    if(name=="_AIL_ms_count@0"){ret(uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(guestClock()).count()),0);return true;}
    if(name=="_AIL_last_error@0"){
        ret(audioText("Android audio output unavailable"),0);return true;
    }
    if(name=="_AIL_open_digital_driver@16"){
        // The headless boot probe stays silent; only visual sessions own a device.
        if(!audio&&runControl){audio=std::make_unique<AudioMixer>(arg(0)?arg(0):44100);if(!audio->openOutput())audio.reset();
            else if(audioDebug>=2)audio->startDump((hostGameRoot+"/audio_dump.f32").c_str(),60);}
        if(audio&&!audioDriver)audioDriver=allocate(64);
        applyMasterVolume();
        __android_log_print(ANDROID_LOG_INFO,"AMRTS","Miles driver %u Hz %u bits %u channels: %s",arg(0),arg(1),arg(2),audio?"AAudio":"unavailable");
        ret(audio?audioDriver:0,4);return true;
    }
    if(name=="_AIL_close_digital_driver@4"){if(arg(0)==audioDriver)resetAudio();ret(0,1);return true;}
    if(name=="_AIL_digital_configuration@16"){
        if(arg(1))write32(arg(1),audio?audio->outputRate():0);
        if(arg(2))write32(arg(2),3);// DIG_F_STEREO_16
        if(arg(3)){const char text[]="Android AAudio";std::memcpy(kernelPtr(arg(3),sizeof text,true),text,sizeof text);}
        ret(0,4);return true;
    }
    if(name=="_AIL_digital_CPU_percent@4"){ret(0,1);return true;}
    if(name=="_AIL_digital_master_volume@4"){ret(uint32_t(gameMasterVolume),1);return true;}
    if(name=="_AIL_set_digital_master_volume@8"){gameMasterVolume=std::clamp(int32_t(arg(1)),0,127);applyMasterVolume();ret(0,2);return true;}
    if(name=="_AIL_allocate_sample_handle@4"){
        if(!audio||arg(0)!=audioDriver||audioSamples.size()>=audioSampleLimit){ret(0,1);return true;}
        uint32_t handle=allocate(256);std::memset(kernelPtr(handle,256,true),0,256);
        audioSamples.push_back(handle);audio->allocate(handle);ret(handle,1);return true;
    }
    uint32_t handle=arg(0);
    if(name=="_AIL_release_sample_handle@4"){
        if(audioSample(handle)){audio->release(handle);audioSamples.erase(std::find(audioSamples.begin(),audioSamples.end(),handle));heaps->free(GuestHeaps::process,handle);}
        ret(0,1);return true;
    }
    if(name=="_AIL_sample_status@4"){applyMasterVolume();ret(audioSample(handle)?audio->status(handle):uint32_t(AudioMixer::Free),1);return true;}
    if(name=="_AIL_sample_position@4"){ret(audioSample(handle)?audio->position(handle):0,1);return true;}
    if(name=="_AIL_init_sample@4"){if(audioSample(handle))audio->init(handle);ret(0,1);return true;}
    if(name=="_AIL_start_sample@4"){if(audioSample(handle))audio->start(handle);ret(0,1);return true;}
    if(name=="_AIL_resume_sample@4"){if(audioSample(handle))audio->resume(handle);ret(0,1);return true;}
    if(name=="_AIL_end_sample@4"){if(audioSample(handle))audio->end(handle);ret(0,1);return true;}
    if(name=="_AIL_set_sample_volume@8"){if(audioSample(handle))audio->setVolume(handle,int32_t(arg(1)));ret(0,2);return true;}
    if(name=="_AIL_set_sample_pan@8"){if(audioSample(handle))audio->setPan(handle,int32_t(arg(1)));ret(0,2);return true;}
    if(name=="_AIL_set_sample_loop_count@8"){if(audioSample(handle))audio->setLoopCount(handle,int32_t(arg(1)));ret(0,2);return true;}
    if(name=="_AIL_set_sample_position@8"){if(audioSample(handle))audio->setPosition(handle,arg(1));ret(0,2);return true;}
    if(name=="_AIL_set_sample_file@12"){
        // A RIFF image carries its own length; anything else needs a name.
        uint32_t image=arg(1),result=0;
        if(audioSample(handle)&&image){
            auto* header=static_cast<const uint8_t*>(kernelPtr(image,12));uint32_t riff;std::memcpy(&riff,header+4,4);
            if(!std::memcmp(header,"RIFF",4)&&riff<0x8000000u)
                result=audio->load(handle,static_cast<const uint8_t*>(kernelPtr(image,riff+8)),riff+8,false)?1:0;
        }
        ret(result,3);return true;
    }
    if(name=="_AIL_set_named_sample_file@20"){
        uint32_t image=arg(2),size=arg(3),result=0;
        if(audioSample(handle)&&image&&size&&size<0x8000000u){
            std::string file=arg(1)?lower(guestString(arg(1))):"";
            bool mpeg=file.size()>=4&&file.compare(file.size()-4,4,".mp3")==0;
            result=audio->load(handle,static_cast<const uint8_t*>(kernelPtr(image,size)),size,mpeg)?1:0;
            if(!result)__android_log_print(ANDROID_LOG_WARN,"AMRTS","Miles could not decode %s (%u bytes)",file.c_str(),size);
        }
        ret(result,5);return true;
    }
    throw GuestStop("Miles function pending: "+name);
}
