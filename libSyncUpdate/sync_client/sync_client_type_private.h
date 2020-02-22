//  sync_client_type_private.h
//  sync_client
//  Created by housisong on 2019-09-17.
/*
 The MIT License (MIT)
 Copyright (c) 2019-2019 HouSisong
 
 Permission is hereby granted, free of charge, to any person
 obtaining a copy of this software and associated documentation
 files (the "Software"), to deal in the Software without
 restriction, including without limitation the rights to use,
 copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following
 conditions:
 
 The above copyright notice and this permission notice shall be
 included in all copies of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef sync_client_type_private_h
#define sync_client_type_private_h
#include "sync_client_type.h"
#include "../../libHDiffPatch/HDiff/private_diff/limit_mem_diff/adler_roll.h"

namespace sync_private{

hpatch_inline static void
TNewDataSyncInfo_init(TNewDataSyncInfo* self) { memset(self,0,sizeof(*self)); }
    
hpatch_inline static
hpatch_StreamPos_t TNewDataSyncInfo_blockCount(const TNewDataSyncInfo* self){
        return getSyncBlockCount(self->newDataSize,self->kMatchBlockSize); }

hpatch_inline static
uint32_t TNewDataSyncInfo_newDataBlockSize(const TNewDataSyncInfo* self,uint32_t blockIndex){
    uint32_t blockCount=(uint32_t)TNewDataSyncInfo_blockCount(self);
    if (blockIndex+1!=blockCount)
        return self->kMatchBlockSize;
    else
        return (uint32_t)(self->newDataSize-self->kMatchBlockSize*blockIndex);
}
hpatch_inline static
uint32_t TNewDataSyncInfo_syncBlockSize(const TNewDataSyncInfo* self,uint32_t blockIndex){
    if (self->savedSizes)
        return self->savedSizes[blockIndex];
    else
        return TNewDataSyncInfo_newDataBlockSize(self,blockIndex);
}
    
inline static uint64_t roll_hash_start(uint64_t*,const adler_data_t* pdata,size_t n){
                                        return fast_adler64_start(pdata,n); }
inline static uint32_t roll_hash_start(uint32_t*,const adler_data_t* pdata,size_t n){
                                        return fast_adler32_start(pdata,n); }
inline static uint64_t roll_hash_roll(uint64_t adler,size_t blockSize,
                                      adler_data_t out_data,adler_data_t in_data){
                                        return fast_adler64_roll(adler,blockSize,out_data,in_data); }
inline static uint32_t roll_hash_roll(uint32_t adler,size_t blockSize,
                                      adler_data_t out_data,adler_data_t in_data){
                                        return fast_adler32_roll(adler,blockSize,out_data,in_data); }
    
#define kStrongChecksumByteSize_min     8
    
hpatch_inline static
void toPartChecksum(unsigned char* out_partChecksum,size_t outPartSize,
                    const unsigned char* checksum,size_t kChecksumByteSize){
    assert(outPartSize<=kChecksumByteSize);
    if (out_partChecksum!=checksum)
        memmove(out_partChecksum,checksum,outPartSize);
    for (size_t oi=0,i=outPartSize;i<kChecksumByteSize; ++i,++oi) {
        if (oi==outPartSize) oi=0;
        out_partChecksum[oi]^=checksum[i];
    }
}

static hpatch_inline
size_t checkChecksumBufByteSize(size_t kStrongChecksumByteSize){
        return kStrongChecksumByteSize*3; }
static hpatch_inline
void checkChecksumInit(unsigned char* checkChecksumBuf,size_t kStrongChecksumByteSize){
        assert((kStrongChecksumByteSize>0)&((kStrongChecksumByteSize%sizeof(uint32_t))==0));
        memset(checkChecksumBuf+kStrongChecksumByteSize,0,kStrongChecksumByteSize*2); }

    static hpatch_inline
    uint32_t _readUInt32(const unsigned char* src){ return src[0] | (src[1]<<8) | (src[2]<<16)| (src[3]<<24); }
    static hpatch_inline
    void _writeUInt32(unsigned char* dst,uint32_t v){
        dst[0]=(unsigned char)v;       dst[1]=(unsigned char)(v>>8);
        dst[2]=(unsigned char)(v>>16); dst[3]=(unsigned char)(v>>24); }
static hpatch_inline
void checkChecksumAppendData(unsigned char* checkChecksumBuf,uint32_t checksumIndex,
                             const unsigned char* strongChecksum,size_t kStrongChecksumByteSize){
    unsigned char* d_xor=checkChecksumBuf+kStrongChecksumByteSize;
    unsigned char* d_xor_end=d_xor+kStrongChecksumByteSize;
    unsigned char* d_sum=d_xor_end;
    for (;d_xor<d_xor_end;d_sum+=sizeof(uint32_t),strongChecksum+=sizeof(uint32_t),
                          checksumIndex<<=1,d_xor+=sizeof(uint32_t)){
        uint32_t src_v = _readUInt32(strongChecksum);
        _writeUInt32(d_xor,_readUInt32(d_xor) ^ (src_v+checksumIndex));
        _writeUInt32(d_sum,_readUInt32(d_sum) + (src_v^checksumIndex));
    }
}
static hpatch_inline
void checkChecksumEndTo(unsigned char* dst,
                        const unsigned char* checkChecksumBuf,size_t kStrongChecksumByteSize){
    const unsigned char* d_xor=checkChecksumBuf+kStrongChecksumByteSize;
    const unsigned char* d_xor_end=d_xor+kStrongChecksumByteSize;
    const unsigned char* sum_r=d_xor_end+kStrongChecksumByteSize-sizeof(uint32_t);
    for (;d_xor<d_xor_end;dst+=sizeof(uint32_t),sum_r-=sizeof(uint32_t),d_xor+=sizeof(uint32_t)){
        uint32_t xor_v; memcpy(&xor_v,d_xor,sizeof(uint32_t));
        uint32_t sum_v; memcpy(&sum_v,sum_r,sizeof(uint32_t));
        xor_v^=sum_v;
        memcpy(dst,&xor_v,sizeof(uint32_t));
    }
}
static hpatch_inline
void checkChecksumEnd(unsigned char* checkChecksumBuf,size_t kStrongChecksumByteSize){
    return checkChecksumEndTo(checkChecksumBuf,checkChecksumBuf,kStrongChecksumByteSize); }


hpatch_inline static unsigned int upper_ilog2(long double v){
    unsigned int bit=0;
    long double p=1;
    while (p<v){ ++bit; p*=2; }
    return bit;
}
    
hpatch_inline static
unsigned int getBetterCacheBlockTableBit(uint32_t blockCount){
    const int kMinBit = 8;
    const int kMaxBit = 23;
    int result=(int)upper_ilog2((1<<kMinBit)+blockCount)-2;
    result=(result<kMinBit)?kMinBit:result;
    result=(result>kMaxBit)?kMaxBit:result;
    return result;
}

} //namespace sync_private
#endif //sync_client_type_private_h
