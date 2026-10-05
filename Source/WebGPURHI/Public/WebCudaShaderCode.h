#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

// WCSH v1/v2 compute and v3 raster are the compiler/runtime ABI. Pass only FShaderCodeReader's actual
// shader bytes here: Unreal's optional metadata trailer is not part of WCSH.
namespace UEWebGPU {
// Kinds: root constants, read buffer, read/write buffer, uniform buffer,
// sampled texture, storage texture, sampler. V1 uses only the first three.
struct ShaderResource {
    uint32_t Binding, Kind;
    uint32_t Dimension=0, Format=0, Access=0, SampleType=0, MinBytes=0, Stride=0;
};
struct ConstantMapping { uint32_t Source, Destination, Bytes; };
struct ResourceTableBinding { uint32_t UniformSlot, ResourceIndex, Binding, Kind; };
struct ShaderCode {
    uint32_t Version=1, Stage=1, ResourceTableBits=0; // 1 compute, 2 vertex, 3 pixel.
    uint32_t BindGroup() const { return Stage==3?1:0; }
    uint32_t Group[3]{}, SourceConstantBytes = 0, ConstantBytes = 0;
    std::vector<ShaderResource> Resources;
    std::vector<ConstantMapping> Mappings;
    std::vector<uint32_t> ResourceTableLayoutHashes;
    std::vector<ResourceTableBinding> ResourceTableBindings;
    std::string Entry, WGSL;

    static bool Decode(const uint8_t* Data, size_t Bytes, ShaderCode& Out, std::string& Error) {
        auto Reject = [&](const char* Why) { Error = Why; return false; };
        if (!Data || Bytes < 48) return Reject("Truncated WebCuda shader header");
        auto Word = [&](size_t P) -> uint32_t {
            return uint32_t(Data[P]) | (uint32_t(Data[P+1])<<8) | (uint32_t(Data[P+2])<<16) | (uint32_t(Data[P+3])<<24);
        };
        if(Word(0)==0x48534357 && (Word(4)==2 || Word(4)==3)) return DecodeV2(Data,Bytes,Out,Error);
        if (Word(0)!=0x48534357 || Word(4)!=1 || Word(8)!=1) return Reject("Unsupported WebCuda shader format/version/stage");
        ShaderCode S;
        uint64_t Threads=1;
        for (size_t I=0;I<3;++I) {
            S.Group[I]=Word(12+I*4);
            if (!S.Group[I] || S.Group[I]>1024) return Reject("Invalid compute workgroup dimension");
            Threads*=S.Group[I];
        }
        if (Threads>1024) return Reject("Invalid compute workgroup size");
        S.SourceConstantBytes=Word(24); S.ConstantBytes=Word(28);
        const uint32_t ResourceCount=Word(32), MappingCount=Word(36), EntryBytes=Word(40), CodeBytes=Word(44);
        if (ResourceCount>16 || MappingCount>16384 || S.SourceConstantBytes>65536 || S.ConstantBytes>65536 ||
            !EntryBytes || EntryBytes>256 || !CodeBytes || CodeBytes>16*1024*1024)
            return Reject("WebCuda shader exceeds ABI limits");
        const uint64_t Expected=48ull+uint64_t(ResourceCount)*8+uint64_t(MappingCount)*12+EntryBytes+CodeBytes;
        if (Expected!=Bytes) return Reject("WebCuda shader length mismatch");
        size_t P=48; uint32_t Seen=0, Uniforms=0;
        for (uint32_t I=0;I<ResourceCount;++I,P+=8) {
            ShaderResource R{Word(P),Word(P+4)};
            if (R.Binding>=16 || R.Kind>2 || (Seen&(1u<<R.Binding))) return Reject("Invalid or duplicate WebCuda resource binding");
            Seen|=1u<<R.Binding; Uniforms+=R.Kind==0; S.Resources.push_back(R);
        }
        if (Uniforms>1 || bool(Uniforms)!=bool(S.ConstantBytes) || bool(S.ConstantBytes)!=bool(S.SourceConstantBytes) ||
            (S.ConstantBytes%16) || bool(MappingCount)!=bool(Uniforms)) return Reject("Inconsistent WebCuda root constants");
        std::vector<uint8_t> SourceUsed(S.SourceConstantBytes), DestinationUsed(S.ConstantBytes);
        for (uint32_t I=0;I<MappingCount;++I,P+=12) {
            ConstantMapping M{Word(P),Word(P+4),Word(P+8)};
            if (!M.Bytes || M.Bytes>16 || M.Bytes%4 || M.Source%4 || M.Destination%4 ||
                uint64_t(M.Source)+M.Bytes>S.SourceConstantBytes || uint64_t(M.Destination)+M.Bytes>S.ConstantBytes)
                return Reject("WebCuda constant mapping outside buffer");
            for (uint32_t B=0;B<M.Bytes;++B) {
                if (SourceUsed[M.Source+B]++ || DestinationUsed[M.Destination+B]++) return Reject("Overlapping WebCuda constant mappings");
            }
            S.Mappings.push_back(M);
        }
        S.Entry.assign(reinterpret_cast<const char*>(Data+P),EntryBytes); P+=EntryBytes;
        S.WGSL.assign(reinterpret_cast<const char*>(Data+P),CodeBytes);
        for (size_t I=0;I<S.Entry.size();++I) {
            const char C=S.Entry[I];
            if (!(C=='_' || (C>='a'&&C<='z') || (C>='A'&&C<='Z') || (I&&C>='0'&&C<='9')))
                return Reject("Invalid WebCuda entry identifier");
        }
        if (S.WGSL.find('\0')!=std::string::npos) return Reject("Embedded null in WebCuda shader source");
        Out=std::move(S); Error.clear(); return true;
    }

    static bool DecodeV2(const uint8_t* Data,size_t Bytes,ShaderCode& Out,std::string& Error) {
        auto Reject=[&](const char* Why){Error=Why;return false;};
        if(!Data || Bytes<64)return Reject("Truncated WebCuda v2 header");
        auto Word=[&](size_t P){return uint32_t(Data[P])|(uint32_t(Data[P+1])<<8)|(uint32_t(Data[P+2])<<16)|(uint32_t(Data[P+3])<<24);};
        const bool Compute=Word(4)==2 && Word(8)==1,Raster=Word(4)==3 && (Word(8)==2 || Word(8)==3);
        if(Word(0)!=0x48534357 || (!Compute&&!Raster) || Word(60))return Reject("Invalid WebCuda extended header/stage");
        ShaderCode S;S.Version=Word(4);S.Stage=Word(8);uint64_t Threads=1;
        for(uint32_t I=0;I<3;++I){S.Group[I]=Word(12+4*I);if(Compute?(!S.Group[I] || S.Group[I]>1024):S.Group[I]!=0)return Reject("Invalid stage workgroup dimensions");Threads*=S.Group[I];}
        if(Compute && Threads>1024)return Reject("Invalid workgroup size");
        S.SourceConstantBytes=Word(24);S.ConstantBytes=Word(28);S.ResourceTableBits=Word(48);
        const uint32_t NR=Word(32),NM=Word(36),NE=Word(40),NC=Word(44),NH=Word(52),NT=Word(56);
        if(NR>64 || NM>16384 || NH>32 || NT>64 || S.SourceConstantBytes>65536 || S.ConstantBytes>65536 ||
           !NE || NE>256 || !NC || NC>16*1024*1024)return Reject("WebCuda v2 limits exceeded");
        if(64ull+32ull*NR+12ull*NM+4ull*NH+16ull*NT+NE+NC!=Bytes)return Reject("WebCuda v2 length mismatch");
        size_t P=64;uint64_t Seen=0;uint32_t Roots=0;
        for(uint32_t I=0;I<NR;++I,P+=32){
            ShaderResource R{Word(P),Word(P+4),Word(P+8),Word(P+12),Word(P+16),Word(P+20),Word(P+24),Word(P+28)};
            if(R.Binding>=64 || R.Kind>6 || (Seen&(1ull<<R.Binding)))return Reject("Invalid v2 binding");
            Seen|=1ull<<R.Binding;
            if(R.Kind<=3){
                if(R.Dimension || R.Format || R.Access || R.SampleType || !R.MinBytes || R.MinBytes%4)return Reject("Invalid buffer descriptor");
                if(R.Kind==0 || R.Kind==3){if(R.Binding>=32 || R.MinBytes>65536 || R.MinBytes%16 || R.Stride)return Reject("Invalid uniform descriptor");}
                else if(!R.Stride || R.Stride%4 || R.Stride>16 || R.MinBytes<R.Stride)return Reject("Invalid storage stride");
                if(R.Kind==0){++Roots;if(R.Binding!=0 || R.MinBytes!=S.ConstantBytes)return Reject("Invalid root descriptor");}
            }else if(R.Kind==4 || R.Kind==5){
                if(R.Dimension<1 || R.Dimension>2 || R.MinBytes || R.Stride)return Reject("Invalid texture descriptor");
                if(R.Kind==4){if(R.Format || R.Access || R.SampleType<1 || R.SampleType>3)return Reject("Invalid sampled texture descriptor");}
                else if(R.SampleType || R.Format<1 || R.Format>3 || R.Access<1 || R.Access>3 || (R.Format!=2 && R.Access!=2))return Reject("Invalid storage texture descriptor");
            }else if(R.Dimension || R.Format || R.Access || R.SampleType || R.MinBytes || R.Stride)return Reject("Invalid sampler descriptor");
            S.Resources.push_back(R);
        }
        if(Roots>1 || bool(Roots)!=bool(S.ConstantBytes) || bool(Roots)!=bool(S.SourceConstantBytes) || bool(Roots)!=bool(NM) || S.ConstantBytes%16)return Reject("Invalid v2 root constants");
        std::vector<uint8_t> SourceUsed(S.SourceConstantBytes),DestUsed(S.ConstantBytes);
        for(uint32_t I=0;I<NM;++I,P+=12){
            ConstantMapping M{Word(P),Word(P+4),Word(P+8)};
            if(!M.Bytes || M.Bytes>16 || M.Bytes%4 || M.Source%4 || M.Destination%4 || uint64_t(M.Source)+M.Bytes>S.SourceConstantBytes || uint64_t(M.Destination)+M.Bytes>S.ConstantBytes)return Reject("Invalid v2 constant mapping");
            for(uint32_t B=0;B<M.Bytes;++B)if(SourceUsed[M.Source+B]++ || DestUsed[M.Destination+B]++)return Reject("Overlapping v2 constants");
            S.Mappings.push_back(M);
        }
        for(uint32_t I=0;I<NH;++I,P+=4)S.ResourceTableLayoutHashes.push_back(Word(P));
        uint32_t TableBits=0;uint64_t TableBindings=0;
        for(uint32_t I=0;I<NT;++I,P+=16){
            ResourceTableBinding T{Word(P),Word(P+4),Word(P+8),Word(P+12)};
            if(T.UniformSlot>=NH || T.UniformSlot>=32 || T.ResourceIndex>=65536 || T.Binding>=64 || (TableBindings&(1ull<<T.Binding)))return Reject("Invalid resource-table entry");
            const ShaderResource* R=nullptr;for(const auto& Candidate:S.Resources)if(Candidate.Binding==T.Binding)R=&Candidate;
            if(!R || !((T.Kind==1&&(R->Kind==1||R->Kind==4)) || (T.Kind==2&&(R->Kind==2||R->Kind==5)) || (T.Kind==4&&R->Kind==4) || (T.Kind==6&&R->Kind==6)))return Reject("Resource table type mismatch");
            TableBits|=1u<<T.UniformSlot;TableBindings|=1ull<<T.Binding;S.ResourceTableBindings.push_back(T);
        }
        if(TableBits!=S.ResourceTableBits)return Reject("Resource table bit mismatch");
        S.Entry.assign(reinterpret_cast<const char*>(Data+P),NE);P+=NE;S.WGSL.assign(reinterpret_cast<const char*>(Data+P),NC);
        for(size_t I=0;I<S.Entry.size();++I){char C=S.Entry[I];if(!(C=='_'||(C>='a'&&C<='z')||(C>='A'&&C<='Z')||(I&&C>='0'&&C<='9')))return Reject("Invalid entry identifier");}
        if(S.WGSL.find('\0')!=std::string::npos)return Reject("Embedded null shader source");
        Out=std::move(S);Error.clear();return true;
    }

    bool EncodeV2(std::vector<uint8_t>& Out,std::string& Error) const {
        // V3 adds raster stages to the otherwise identical extended layout.
        std::vector<uint8_t> Bytes;
        auto Word=[&](uint32_t V){for(uint32_t I=0;I<4;++I)Bytes.push_back(uint8_t(V>>(I*8)));};
        for(uint32_t V:{0x48534357u,Version,Stage,Group[0],Group[1],Group[2],SourceConstantBytes,ConstantBytes,uint32_t(Resources.size()),uint32_t(Mappings.size()),uint32_t(Entry.size()),uint32_t(WGSL.size()),ResourceTableBits,uint32_t(ResourceTableLayoutHashes.size()),uint32_t(ResourceTableBindings.size()),0u})Word(V);
        for(const auto& R:Resources)for(uint32_t V:{R.Binding,R.Kind,R.Dimension,R.Format,R.Access,R.SampleType,R.MinBytes,R.Stride})Word(V);
        for(const auto& M:Mappings)for(uint32_t V:{M.Source,M.Destination,M.Bytes})Word(V);
        for(uint32_t H:ResourceTableLayoutHashes)Word(H);
        for(const auto& T:ResourceTableBindings)for(uint32_t V:{T.UniformSlot,T.ResourceIndex,T.Binding,T.Kind})Word(V);
        Bytes.insert(Bytes.end(),Entry.begin(),Entry.end());Bytes.insert(Bytes.end(),WGSL.begin(),WGSL.end());
        ShaderCode Check;if(!Decode(Bytes.data(),Bytes.size(),Check,Error))return false;
        Out=std::move(Bytes);return true;
    }

    // Standard Unreal resource-table tokens, for FRHIShaderData consumers.
    // The native compiler checks these against BuildShaderResourceTable.
    std::vector<uint32_t> MakeResourceTableTokens(uint32_t Kind) const {
        std::vector<uint32_t> Tokens;uint32_t Slots=0;
        for(const auto& Item:ResourceTableBindings) {
            Slots=std::max(Slots,Item.UniformSlot+1);
            if(Item.Kind==Kind)Tokens.push_back((Item.UniformSlot<<24)|(Item.ResourceIndex<<8)|Item.Binding);
        }
        if(Tokens.empty())return {};
        std::sort(Tokens.begin(),Tokens.end());
        std::vector<uint32_t> Result(Slots,0);
        for(uint32_t Token:Tokens) {uint32_t Slot=Token>>24;if(!Result[Slot])Result[Slot]=uint32_t(Result.size());Result.push_back(Token);}
        Result.push_back(0xffffffffu);return Result;
    }

    bool PackConstants(const void* Source, size_t Bytes, std::vector<uint8_t>& Packed) const {
        // Unreal uploads through the last used field, which can omit trailing
        // cbuffer padding. Every copied field must still fit the supplied data.
        if ((ConstantBytes&&!Source)) return false;
        for(const auto& M:Mappings) if(uint64_t(M.Source)+M.Bytes>Bytes) return false;
        Packed.assign(ConstantBytes,0);
        for (const auto& M:Mappings) std::memcpy(Packed.data()+M.Destination,static_cast<const uint8_t*>(Source)+M.Source,M.Bytes);
        return true;
    }
};
}
