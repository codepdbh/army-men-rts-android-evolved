#include "audio_mixer.h"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void put16(std::vector<uint8_t>& out,uint32_t value){out.push_back(uint8_t(value));out.push_back(uint8_t(value>>8));}
static void put32(std::vector<uint8_t>& out,uint32_t value){put16(out,value&0xffff);put16(out,value>>16);}
static std::vector<uint8_t> sineWave(uint32_t rate,uint32_t frames){
    std::vector<uint8_t> wav{'R','I','F','F'};put32(wav,36+frames*2);wav.insert(wav.end(),{'W','A','V','E','f','m','t',' '});
    put32(wav,16);put16(wav,1);put16(wav,1);put32(wav,rate);put32(wav,rate*2);put16(wav,2);put16(wav,16);
    wav.insert(wav.end(),{'d','a','t','a'});put32(wav,frames*2);
    for(uint32_t i=0;i<frames;++i)put16(wav,uint16_t(int16_t(std::sin(i*0.05)*16000)));
    return wav;
}
static float peak(AudioMixer& mixer,uint32_t frames,float* left=nullptr,float* right=nullptr){
    std::vector<float> buffer(frames*2);mixer.render(buffer.data(),frames);float result=0,l=0,r=0;
    for(uint32_t i=0;i<frames;++i){l=std::max(l,std::fabs(buffer[i*2]));r=std::max(r,std::fabs(buffer[i*2+1]));}
    if(left)*left=l;if(right)*right=r;result=std::max(l,r);return result;
}
// Three consecutive MPEG-1 Layer III frame headers, so a stray 0xFFE is not taken for audio.
static size_t findMpeg(const std::vector<uint8_t>& data){
    static const int bitrates[]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
    static const int rates[]={44100,48000,32000,0};
    auto frameAt=[&](size_t at)->size_t{
        if(at+4>data.size()||data[at]!=0xff||(data[at+1]&0xfe)!=0xfa)return 0;
        int bitrate=bitrates[data[at+2]>>4],rate=rates[(data[at+2]>>2)&3];if(!bitrate||!rate)return 0;
        return size_t(144000*bitrate/rate+((data[at+2]>>1)&1));
    };
    for(size_t at=0;at+4<data.size();++at){
        size_t first=frameAt(at);if(!first)continue;size_t second=frameAt(at+first);if(!second)continue;
        if(frameAt(at+first+second))return at;
    }
    return SIZE_MAX;
}
int main(int argc,char** argv){
    try{
        AudioMixer mixer(44100);
        require(peak(mixer,256)==0,"Idle mixer is not silent");
        auto wav=sineWave(22050,22050);
        mixer.allocate(0x1000);require(mixer.status(0x1000)==AudioMixer::Done,"New sample is not done");
        require(mixer.load(0x1000,wav.data(),wav.size(),false),"PCM WAV rejected");
        require(peak(mixer,256)==0,"Loaded sample played before start");
        mixer.start(0x1000);require(mixer.status(0x1000)==AudioMixer::Playing,"Sample did not start");
        require(peak(mixer,4410)>0.3f,"Started sample is silent");
        require(mixer.position(0x1000)>4000&&mixer.position(0x1000)<4800,"22 kHz sample was not resampled to 44.1 kHz");
        float left,right;mixer.setPan(0x1000,0);peak(mixer,64);peak(mixer,512,&left,&right);require(left>0.3f&&right==0,"Hard-left pan leaked to the right");
        mixer.setPan(0x1000,64);mixer.setVolume(0x1000,0);peak(mixer,64);require(peak(mixer,512)==0,"Zero volume is audible");
        mixer.setVolume(0x1000,127);mixer.setMasterVolume(0);peak(mixer,64);require(peak(mixer,512)==0,"Zero master volume is audible");
        mixer.setMasterVolume(127);
        peak(mixer,44100*2);require(mixer.status(0x1000)==AudioMixer::Done,"One-shot sample did not finish");
        mixer.setLoopCount(0x1000,2);mixer.start(0x1000);peak(mixer,66150);
        require(mixer.status(0x1000)==AudioMixer::Playing,"Second loop iteration was skipped");peak(mixer,44100);
        require(mixer.status(0x1000)==AudioMixer::Done,"Finite loop count never ended");
        mixer.setLoopCount(0x1000,0);mixer.start(0x1000);peak(mixer,44100*5);require(mixer.status(0x1000)==AudioMixer::Playing,"Infinite loop ended");
        mixer.end(0x1000);require(mixer.status(0x1000)==AudioMixer::Done&&peak(mixer,256)==0,"Ended sample still plays");
        mixer.setPosition(0x1000,22050);mixer.resume(0x1000);require(mixer.position(0x1000)==22050,"Seek position lost");
        require(peak(mixer,256)>0,"Resume after seek is silent");
        const uint8_t junk[]={1,2,3,4,5,6,7,8};require(!mixer.load(0x1000,junk,sizeof junk,false),"Garbage accepted as audio");
        mixer.release(0x1000);require(mixer.status(0x1000)==AudioMixer::Free&&mixer.voices()==0,"Released sample still allocated");
        if(argc>1){
            std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),{});
            size_t at=findMpeg(data);require(at!=SIZE_MAX,"No MPEG audio in the game pack");
            size_t size=std::min<size_t>(data.size()-at,512*1024);
            mixer.allocate(0x2000);require(mixer.load(0x2000,data.data()+at,size,true),"Game MP3 rejected");
            mixer.start(0x2000);float loudest=0;for(int i=0;i<40;++i)loudest=std::max(loudest,peak(mixer,4410));
            require(loudest>0.01f,"Game MP3 decoded to silence");require(mixer.position(0x2000)>0,"MP3 position did not advance");
            std::cout<<"Game MP3 at offset "<<at<<" peak "<<loudest<<"\n";
        }
        std::cout<<"Miles mixer tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
