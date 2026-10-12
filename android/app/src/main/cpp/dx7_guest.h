#pragma once
#include <cstdint>

// Fixed 32-bit ABI layouts. Native ARM64 pointers never enter these records.
struct DxGuid { uint32_t a;uint16_t b,c;uint8_t d[8]; };
constexpr DxGuid iidUnknown{0,0,0,{0xc0,0,0,0,0,0,0,0x46}};
constexpr DxGuid iidDraw1{0x6c14db80,0xa733,0x11ce,{0xa5,0x21,0,0x20,0xaf,0x0b,0xe5,0x60}};
constexpr DxGuid iidDraw7{0x15e65ec0,0x3b9c,0x11d2,{0xb9,0x2f,0,0x60,0x97,0x97,0xea,0x5b}};
constexpr DxGuid iidDirect3D7{0xf5049e77,0x4861,0x11d2,{0xa4,0x07,0,0xa0,0xc9,0x06,0x29,0xa8}};
constexpr DxGuid iidHalDevice{0x84e63de0,0x46aa,0x11cf,{0x81,0x6f,0,0,0xc0,0x20,0x15,0x6e}};
struct DxPixelFormat { uint32_t size,flags,fourcc,bits,r,g,b,a; };
struct DxSurfaceDesc {
    uint32_t size,flags,height,width,pitch,backbuffers,mips,alphaBits,reserved,surface;
    uint32_t colorKeys[8];DxPixelFormat format;uint32_t caps[4],textureStage;
};
struct DxPrimitiveCaps {
    uint32_t size,misc,raster,zCompare,sourceBlend,destinationBlend,alphaCompare,shade;
    uint32_t texture,textureFilter,textureBlend,textureAddress,stippleWidth,stippleHeight;
};
struct DxDeviceDesc7 {
    uint32_t caps;DxPrimitiveCaps lines,triangles;uint32_t renderBits,zBits;
    uint32_t minWidth,minHeight,maxWidth,maxHeight,maxRepeat,maxAspect,maxAnisotropy;
    float guardLeft,guardTop,guardRight,guardBottom,extents;
    uint32_t stencil,fvf,textureOps;uint16_t blendStages,simultaneousTextures;
    uint32_t activeLights;float maxVertexW;DxGuid guid;uint16_t clipPlanes,blendMatrices;
    uint32_t vertexProcessing,reserved[4];
};
static_assert(sizeof(DxGuid)==16&&sizeof(DxPixelFormat)==32&&sizeof(DxSurfaceDesc)==124,"DX7 guest record layout");
static_assert(sizeof(DxPrimitiveCaps)==56&&sizeof(DxDeviceDesc7)==236,"DX7 guest capability layout");
