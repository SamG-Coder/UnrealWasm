#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

namespace UEWebGPU {
// Preserve the native uniform layout, with resource pointer slots kept out of
// GPU constants. Resources may live beyond ConstantBufferSize in UE layouts;
// those entries are handled only by the separate resource table.
inline bool PackUniformData(const void* Source,uint32_t Bytes,const uint32_t* ResourceOffsets,size_t ResourceCount,std::vector<uint8_t>& Out) {
    if(Bytes>65536 || (Bytes&&!Source) || (ResourceCount&&!ResourceOffsets) || ResourceCount>8192)return false;
    std::vector<uint8_t> Packed((Bytes+15u)&~15u,0);
    if(Bytes)std::memcpy(Packed.data(),Source,Bytes);
    std::vector<uint8_t> Used(Bytes,0);
    for(size_t I=0;I<ResourceCount;++I) {
        uint32_t Offset=ResourceOffsets[I];
        if(Offset%8)return false; // UE's shader parameter resource alignment.
        if(Offset>=Bytes)continue;
        if(Bytes-Offset<8)return false;
        for(uint32_t B=0;B<8;++B)if(Used[Offset+B]++)return false;
        std::memset(Packed.data()+Offset,0,8);
    }
    Out=std::move(Packed);return true;
}
}
