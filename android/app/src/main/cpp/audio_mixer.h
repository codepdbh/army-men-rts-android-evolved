#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>

// Miles Sound System 6 digital samples rendered by a host mixer. Handles are
// guest addresses chosen by the caller; sample data is copied on load so the
// mixer never reads guest memory from the audio thread.
class AudioMixer {
public:
    enum Status : uint32_t {Free=1,Done=2,Playing=4,Stopped=8};
    explicit AudioMixer(uint32_t outputRate=44100);
    ~AudioMixer();
    AudioMixer(const AudioMixer&)=delete;
    AudioMixer& operator=(const AudioMixer&)=delete;

    // Android output; returns false when no device is available.
    bool openOutput();
    void closeOutput();
    uint32_t outputRate() const {return rate_;}

    void allocate(uint32_t handle);
    void release(uint32_t handle);
    void init(uint32_t handle);
    // WAV (PCM 8/16-bit) or MPEG audio. False if unsupported.
    bool load(uint32_t handle,const uint8_t* data,size_t size,bool mpegHint);
    void start(uint32_t handle);
    void resume(uint32_t handle);
    void end(uint32_t handle);
    uint32_t status(uint32_t handle);
    uint32_t position(uint32_t handle);
    void setPosition(uint32_t handle,uint32_t bytes);
    void setVolume(uint32_t handle,int32_t volume);
    void setPan(uint32_t handle,int32_t pan);
    void setLoopCount(uint32_t handle,int32_t count);
    void setMasterVolume(int32_t volume);
    int32_t masterVolume();
    size_t voices();

    // Debug capture of the final mix (raw float stereo) for offline analysis.
    void startDump(const char* path,uint32_t seconds);
    // Interleaved stereo float frames; also used directly by host tests.
    void render(float* output,uint32_t frames);
    struct Voice;
private:
    void restartOutput();
    std::mutex mutex_;
    std::map<uint32_t,std::unique_ptr<Voice>> voices_;
    uint32_t rate_;
    int32_t master_=127;
    void* stream_=nullptr;
    bool restarting_=false;
    void* dump_=nullptr;
    uint64_t dumpFrames_=0;
    uint64_t xrunsLogged_=0;
    friend struct AudioCallbacks;
};
