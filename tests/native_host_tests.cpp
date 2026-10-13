#include "vulkan_renderer.h"
#include "guest_run_control.h"
#include <limits>
#include <iostream>
#include <thread>

static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    try{
        GuestRunControl control;
        control.pointer(.2f,.3f,0,true);control.pointer(.4f,.5f,3,false);control.pointer(.6f,.7f,3,false);control.pointer(.6f,.7f,0,false);
        require(control.pending.size()==3,"Pointer coalescing lost a button transition");
        require(control.pending.front().down&&!control.pending.back().down,"Tap event order changed");
        control.pointer(std::numeric_limits<float>::quiet_NaN(),0,3,false);control.key(256,true);
        require(control.pending.size()==3,"Invalid Android input accepted");
        control.pending.clear();
        std::thread producer([&]{for(unsigned i=0;i<1000;++i){control.key(28,true);control.key(28,false);}});
        producer.join();require(control.pending.size()==2000,"Concurrent input lost events");
        control.stopped.store(true);control.key(1,true);require(control.pending.size()==2000,"Closed session accepted input");
        GuestRunControl overflow;for(unsigned i=0;i<4097;++i)overflow.key(28,bool(i&1));
        require(overflow.stopped.load()&&overflow.pending.empty(),"Overflow left stuck buttons in an active session");
        VulkanRenderer renderer;renderer.initialize();renderer.testClearReadback();renderer.testTriangleReadback();
        auto target=renderer.createSurface(64,64,false,32),depth=renderer.createSurface(64,64,true,16),texture=renderer.createSurface(1,1,false,32);
        uint32_t red=0xff0000ff;renderer.uploadSurface(*texture,&red);renderer.clearSurface(*target,0xff000000,1);renderer.clearSurface(*depth,0,1);
        VulkanDrawState state;state.cull=1;state.zTest=state.zWrite=true;state.viewport[2]=state.viewport[3]=64;state.viewport[5]=1;
        std::vector<VulkanVertex> vertices(3);float positions[3][4]={{-.9f,-.9f,.25f,1},{.9f,-.9f,.25f,1},{0,.9f,.25f,1}};
        for(unsigned i=0;i<3;++i){std::copy(positions[i],positions[i]+4,vertices[i].position);std::fill(vertices[i].color,vertices[i].color+4,1);}
        renderer.draw(*target,depth.get(),texture.get(),nullptr,vertices,{0,1,2},state);
        auto pixels=renderer.readSurface(*target);unsigned center=(32*64+32)*4;
        require(pixels[center]==255&&pixels[center+1]==0&&pixels[center+2]==0,"Texture modulation failed");
        state.stages[0]=2;state.stages[1]=0;state.stages[3]=2;
        for(auto& vertex:vertices){vertex.position[2]=.75f;vertex.color[0]=vertex.color[1]=0;vertex.color[2]=1;}
        renderer.draw(*target,depth.get(),nullptr,nullptr,vertices,{0,1,2},state);pixels=renderer.readSurface(*target);
        require(pixels[center]==255&&pixels[center+2]==0,"Depth buffer did not reject the farther triangle");
        state.zTest=state.zWrite=false;state.blend=true;state.sourceBlend=5;state.destinationBlend=6;
        for(auto& vertex:vertices){vertex.color[0]=vertex.color[2]=0;vertex.color[1]=1;vertex.color[3]=.5f;}
        renderer.draw(*target,nullptr,nullptr,nullptr,vertices,{0,1,2},state);pixels=renderer.readSurface(*target);
        require(pixels[center]>=126&&pixels[center]<=129&&pixels[center+1]>=126&&pixels[center+1]<=129,"Alpha blending failed");
        state.viewport[2]=state.viewport[3]=32;
        renderer.clearSurface(*target,0xff000000,1);renderer.draw(*target,nullptr,nullptr,nullptr,vertices,{0,1,2},state);pixels=renderer.readSurface(*target);
        require(pixels[(16*64+16)*4+1]>100&&pixels[center+1]==0,"Cached pipeline retained an old viewport");
        renderer.clearSurface(*target,0xff000000,1);
        renderer.clearRegions(*target,depth.get(),3,0xff00ff00,.5f,{{{16,16},{16,16}}});pixels=renderer.readSurface(*target);
        require(pixels[(20*64+20)*4+1]==255&&pixels[(10*64+10)*4+1]==0,"Region clear overwrote pixels outside its rectangle");
        std::cout<<"Host input and Vulkan 1.1 pixel tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
