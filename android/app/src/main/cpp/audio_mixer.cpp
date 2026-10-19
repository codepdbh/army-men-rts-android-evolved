#include "audio_mixer.h"
#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD_FALLBACK_WARNING
#include "third_party/minimp3/minimp3.h"
#ifdef __ANDROID__
#include <aaudio/AAudio.h>
#include <android/log.h>
#endif

namespace {
uint16_t le16(const uint8_t* p){return uint16_t(p[0]|p[1]<<8);}
uint32_t le32(const uint8_t* p){return uint32_t(p[0]|p[1]<<8|p[2]<<16|uint32_t(p[3])<<24);}
std::atomic<bool> outputLost{false};
}

struct AudioMixer::Voice {
    uint32_t status=Done;
    int32_t volume=127,pan=64,loops=1,loopsLeft=1;
    bool loaded=false,mpeg=false;
    // PCM WAV, converted to signed 16-bit at load time.
    std::vector<int16_t> pcm;
    uint32_t channels=1,sourceRate=22050,bytesPerFrame=2;
    uint64_t cursor=0;
    // MPEG audio, decoded one frame at a time on the audio thread.
    std::vector<uint8_t> mpegData;
    mp3dec_t decoder{};
    int16_t frame[MINIMP3_MAX_SAMPLES_PER_FRAME]{};
    uint32_t frameSamples=0,frameIndex=0;
    size_t mpegOffset=0,frameOffset=0;
    // Linear resampler state.
    float previous[2]{},current[2]{};
    double phase=1;

    void rewind(){cursor=0;mpegOffset=frameOffset=0;frameSamples=frameIndex=0;mp3dec_init(&decoder);}
    void restart(){rewind();previous[0]=previous[1]=current[0]=current[1]=0;phase=1;}
    bool decodeMpeg(){
        while(mpegOffset<mpegData.size()){
            mp3dec_frame_info_t info{};
            int samples=mp3dec_decode_frame(&decoder,mpegData.data()+mpegOffset,int(mpegData.size()-mpegOffset),frame,&info);
            if(!info.frame_bytes)break;
            frameOffset=mpegOffset;mpegOffset+=size_t(info.frame_bytes);
            if(samples>0&&info.channels>0&&info.hz>0){
                frameSamples=uint32_t(samples);frameIndex=0;channels=uint32_t(info.channels);sourceRate=uint32_t(info.hz);return true;
            }
        }
        mpegOffset=mpegData.size();return false;
    }
    bool available(){
        if(mpeg)return frameIndex<frameSamples||decodeMpeg();
        return cursor<pcm.size()/channels;
    }
    // Next source frame, honouring Miles loop counts (0 = forever).
    bool next(float out[2]){
        if(!available()){
            if(loops!=0&&--loopsLeft<=0)return false;
            rewind();if(!available())return false;
        }
        const int16_t* sample=mpeg?frame+size_t(frameIndex++)*channels:pcm.data()+size_t(cursor++)*channels;
        out[0]=sample[0]*(1.0f/32768);out[1]=channels>1?sample[1]*(1.0f/32768):out[0];
        return true;
    }
};

AudioMixer::AudioMixer(uint32_t outputRate):rate_(outputRate){}
AudioMixer::~AudioMixer(){closeOutput();if(dump_)std::fclose(static_cast<FILE*>(dump_));}

void AudioMixer::allocate(uint32_t handle){std::lock_guard<std::mutex> lock(mutex_);voices_[handle]=std::make_unique<Voice>();}
void AudioMixer::release(uint32_t handle){
    std::unique_ptr<Voice> old;
    {std::lock_guard<std::mutex> lock(mutex_);auto found=voices_.find(handle);if(found==voices_.end())return;old=std::move(found->second);voices_.erase(found);}
}
void AudioMixer::init(uint32_t handle){
    std::unique_ptr<Voice> fresh=std::make_unique<Voice>(),old;
    std::lock_guard<std::mutex> lock(mutex_);auto found=voices_.find(handle);if(found==voices_.end())return;
    old=std::move(found->second);found->second=std::move(fresh);
}

bool AudioMixer::load(uint32_t handle,const uint8_t* data,size_t size,bool mpegHint){
    auto voice=std::make_unique<Voice>();
    if(size>=12&&!std::memcmp(data,"RIFF",4)&&!std::memcmp(data+8,"WAVE",4)){
        size_t riff=std::min<size_t>(size,size_t(le32(data+4))+8),offset=12;
        uint16_t format=0,channels=0,bits=0;uint32_t rate=0;const uint8_t* body=nullptr;size_t bodySize=0;
        while(offset+8<=riff){
            uint32_t chunk=le32(data+offset+4);size_t start=offset+8,available=std::min<size_t>(chunk,riff-start);
            if(!std::memcmp(data+offset,"fmt ",4)&&available>=16){
                format=le16(data+start);channels=le16(data+start+2);rate=le32(data+start+4);bits=le16(data+start+14);
            }else if(!std::memcmp(data+offset,"data",4)){body=data+start;bodySize=available;}
            offset=start+chunk+(chunk&1);
        }
        if(format!=1||!body||(channels!=1&&channels!=2)||(bits!=8&&bits!=16)||rate<1000||rate>192000)return false;
        voice->channels=channels;voice->sourceRate=rate;voice->bytesPerFrame=channels*bits/8;
        size_t samples=bodySize/(bits/8);samples-=samples%channels;voice->pcm.resize(samples);
        for(size_t i=0;i<samples;++i)voice->pcm[i]=bits==16?int16_t(le16(body+i*2)):int16_t((int(body[i])-128)<<8);
    }else{
        if(!mpegHint&&!(size>=3&&(!std::memcmp(data,"ID3",3)||(data[0]==0xff&&(data[1]&0xe0)==0xe0))))return false;
        voice->mpeg=true;voice->mpegData.assign(data,data+size);voice->rewind();
        if(!voice->decodeMpeg())return false;
        voice->restart();
    }
    voice->loaded=true;
    std::unique_ptr<Voice> old;
    std::lock_guard<std::mutex> lock(mutex_);auto found=voices_.find(handle);if(found==voices_.end())return false;
    voice->volume=found->second->volume;voice->pan=found->second->pan;voice->loops=voice->loopsLeft=found->second->loops;
    old=std::move(found->second);found->second=std::move(voice);return true;
}

#define AUDIO_VOICE(handle) std::lock_guard<std::mutex> lock(mutex_);auto found=voices_.find(handle);if(found==voices_.end())
void AudioMixer::start(uint32_t handle){AUDIO_VOICE(handle)return;auto& v=*found->second;if(!v.loaded)return;v.restart();v.loopsLeft=v.loops;v.status=Playing;}
void AudioMixer::resume(uint32_t handle){AUDIO_VOICE(handle)return;auto& v=*found->second;if(v.loaded&&(v.status==Stopped||v.status==Done))v.status=Playing;}
void AudioMixer::end(uint32_t handle){AUDIO_VOICE(handle)return;found->second->status=Done;}
uint32_t AudioMixer::status(uint32_t handle){
    if(outputLost.exchange(false))restartOutput();
#ifdef __ANDROID__
    if(stream_){
        int32_t xruns=AAudioStream_getXRunCount(static_cast<AAudioStream*>(stream_));
        if(xruns>0&&uint64_t(xruns)>xrunsLogged_){__android_log_print(ANDROID_LOG_WARN,"AMRTS","AAudio underruns: %d",xruns);xrunsLogged_=uint64_t(xruns);}
    }
#endif
    AUDIO_VOICE(handle)return Free;return found->second->status;
}
uint32_t AudioMixer::position(uint32_t handle){
    AUDIO_VOICE(handle)return 0;auto& v=*found->second;
    return uint32_t(v.mpeg?v.frameOffset:v.cursor*v.bytesPerFrame);
}
void AudioMixer::setPosition(uint32_t handle,uint32_t bytes){
    AUDIO_VOICE(handle)return;auto& v=*found->second;v.restart();
    if(v.mpeg)v.mpegOffset=std::min<size_t>(bytes,v.mpegData.size());
    else v.cursor=std::min<uint64_t>(bytes/std::max(v.bytesPerFrame,1u),v.pcm.size()/v.channels);
}
void AudioMixer::setVolume(uint32_t handle,int32_t volume){AUDIO_VOICE(handle)return;found->second->volume=std::clamp(volume,0,127);}
void AudioMixer::setPan(uint32_t handle,int32_t pan){AUDIO_VOICE(handle)return;found->second->pan=std::clamp(pan,0,127);}
void AudioMixer::setLoopCount(uint32_t handle,int32_t count){AUDIO_VOICE(handle)return;found->second->loops=found->second->loopsLeft=std::max(count,0);}
#undef AUDIO_VOICE
void AudioMixer::setMasterVolume(int32_t volume){std::lock_guard<std::mutex> lock(mutex_);master_=std::clamp(volume,0,127);}
int32_t AudioMixer::masterVolume(){std::lock_guard<std::mutex> lock(mutex_);return master_;}
size_t AudioMixer::voices(){std::lock_guard<std::mutex> lock(mutex_);return voices_.size();}

void AudioMixer::render(float* output,uint32_t frames){
    std::fill(output,output+size_t(frames)*2,0.0f);
    std::lock_guard<std::mutex> lock(mutex_);
    for(auto& item:voices_){
        auto& v=*item.second;if(v.status!=Playing)continue;
        float gain=(v.volume/127.0f)*(master_/127.0f);
        float left=gain*std::min(1.0f,(127-v.pan)/63.0f),right=gain*std::min(1.0f,v.pan/64.0f);
        for(uint32_t i=0;i<frames;++i){
            while(v.phase>=1){
                v.previous[0]=v.current[0];v.previous[1]=v.current[1];
                if(!v.next(v.current)){v.status=Done;break;}
                v.phase-=1;
            }
            if(v.status!=Playing)break;
            float t=float(v.phase);
            output[i*2]+=(v.previous[0]+(v.current[0]-v.previous[0])*t)*left;
            output[i*2+1]+=(v.previous[1]+(v.current[1]-v.previous[1])*t)*right;
            v.phase+=double(v.sourceRate)/rate_;
        }
    }
    // Soft knee above -1 dBFS instead of hard clipping when many voices overlap.
    for(size_t i=0;i<size_t(frames)*2;++i){
        float value=output[i],magnitude=std::fabs(value);
        if(magnitude>.89f)value=std::copysign(.89f+.11f*std::tanh((magnitude-.89f)/.11f),value);
        output[i]=value;
    }
    if(dump_){
        uint64_t count=std::min<uint64_t>(frames,dumpFrames_);std::fwrite(output,sizeof(float)*2,size_t(count),static_cast<FILE*>(dump_));
        dumpFrames_-=count;if(!dumpFrames_){std::fclose(static_cast<FILE*>(dump_));dump_=nullptr;}
    }
}
void AudioMixer::startDump(const char* path,uint32_t seconds){
    std::lock_guard<std::mutex> lock(mutex_);if(dump_)return;
    dump_=std::fopen(path,"wb");dumpFrames_=uint64_t(seconds)*rate_;
}

#ifdef __ANDROID__
struct AudioCallbacks {
    static aaudio_data_callback_result_t data(AAudioStream*,void* user,void* audio,int32_t frames){
        static_cast<AudioMixer*>(user)->render(static_cast<float*>(audio),uint32_t(frames));return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    // Closing is not allowed here; the game thread reopens on its next poll.
    static void error(AAudioStream*,void*,aaudio_result_t){outputLost.store(true);}
};
bool AudioMixer::openOutput(){
    closeOutput();
    AAudioStreamBuilder* builder=nullptr;if(AAudio_createStreamBuilder(&builder)!=AAUDIO_OK)return false;
    AAudioStreamBuilder_setDirection(builder,AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder,AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder,AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(builder,AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(builder,2);
    AAudioStreamBuilder_setSampleRate(builder,int32_t(rate_));
    AAudioStreamBuilder_setUsage(builder,AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setContentType(builder,AAUDIO_CONTENT_TYPE_SONIFICATION);
    AAudioStreamBuilder_setDataCallback(builder,AudioCallbacks::data,this);
    AAudioStreamBuilder_setErrorCallback(builder,AudioCallbacks::error,this);
    AAudioStream* stream=nullptr;aaudio_result_t result=AAudioStreamBuilder_openStream(builder,&stream);
    AAudioStreamBuilder_delete(builder);
    if(result!=AAUDIO_OK){__android_log_print(ANDROID_LOG_WARN,"AMRTS","AAudio open failed: %s",AAudio_convertResultToText(result));return false;}
    {std::lock_guard<std::mutex> lock(mutex_);rate_=uint32_t(AAudioStream_getSampleRate(stream));}
    if(AAudioStream_requestStart(stream)!=AAUDIO_OK){AAudioStream_close(stream);return false;}
    // Two bursts of buffering: low latency with headroom for MP3 frame decodes.
    AAudioStream_setBufferSizeInFrames(stream,AAudioStream_getFramesPerBurst(stream)*2);
    stream_=stream;
    __android_log_print(ANDROID_LOG_INFO,"AMRTS","AAudio output %d Hz, burst %d",AAudioStream_getSampleRate(stream),AAudioStream_getFramesPerBurst(stream));
    return true;
}
void AudioMixer::closeOutput(){
    auto* stream=static_cast<AAudioStream*>(stream_);stream_=nullptr;
    if(stream){AAudioStream_requestStop(stream);AAudioStream_close(stream);}
}
void AudioMixer::restartOutput(){if(stream_)openOutput();}
#else
bool AudioMixer::openOutput(){return true;}
void AudioMixer::closeOutput(){}
void AudioMixer::restartOutput(){}
#endif
