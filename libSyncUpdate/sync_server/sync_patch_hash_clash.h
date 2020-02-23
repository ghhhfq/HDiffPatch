//  sync_patch_hash_clash.h
//  sync_server
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
#ifndef sync_patch_hash_clash_h
#define sync_patch_hash_clash_h
#include "../sync_client/sync_client_type.h"
#include "../sync_client/sync_client_type_private.h"

static const size_t kSafeHashClashBit_min     = 20;
static const size_t kSafeHashClashBit_default = 32;

namespace sync_private{
    const size_t _kNeedMinRollHashByte  = 1;
    const size_t _kMaxRollHashByte      = sizeof(uint64_t);
    
    hpatch_inline static
    size_t _estimateCompareCountBit(hpatch_StreamPos_t newDataSize,uint32_t kMatchBlockSize){
        long double blockCount=(long double)getSyncBlockCount(newDataSize,kMatchBlockSize);
        return sync_private::upper_ilog2(newDataSize*blockCount);
    }
    
    hpatch_inline static
    size_t getNeedHashByte(size_t kSafeHashClashBit,hpatch_StreamPos_t newDataSize,uint32_t kMatchBlockSize){
        const size_t _kNeedMinStrongHashByte=(kSafeHashClashBit+7)/8;
        const size_t _kNeedMinHashByte=_kNeedMinRollHashByte+_kNeedMinStrongHashByte;
        size_t compareCountBit=_estimateCompareCountBit(newDataSize,kMatchBlockSize);
        size_t result=(compareCountBit+kSafeHashClashBit+7)/8;
        return (result>=_kNeedMinHashByte)?result:_kNeedMinHashByte;
    }
    hpatch_inline static
    size_t getNeedHashByte(size_t kSafeHashClashBit,hpatch_StreamPos_t newDataSize,uint32_t kMatchBlockSize,
                           size_t kStrongHashByte,size_t* out_partRollHashByte,size_t* out_partStrongHashByte){
        const size_t result=getNeedHashByte(kSafeHashClashBit,newDataSize,kMatchBlockSize);
        assert(kStrongHashByte>=kStrongChecksumByteSize_min);
        size_t compareCountBit=_estimateCompareCountBit(newDataSize,kMatchBlockSize);
        size_t rollHashByte=compareCountBit/8;
        if (rollHashByte>2) --rollHashByte;
        if (rollHashByte<_kNeedMinRollHashByte) rollHashByte=_kNeedMinRollHashByte;
        else if (rollHashByte>_kMaxRollHashByte) rollHashByte=_kMaxRollHashByte;
        assert(rollHashByte<=result);
        size_t strongHashByte=result-rollHashByte;
        while ((strongHashByte*8<kSafeHashClashBit)&&(rollHashByte>_kNeedMinRollHashByte)){
            ++strongHashByte; --rollHashByte;
        }
        if (strongHashByte>kStrongHashByte){
            strongHashByte=kStrongHashByte;
            rollHashByte=result-kStrongHashByte;
            assert((strongHashByte>=_kNeedMinRollHashByte)&(strongHashByte<=_kMaxRollHashByte));
        }
        *out_partRollHashByte=rollHashByte;
        *out_partStrongHashByte=strongHashByte;
        return result;
    }
}//namespace sync_private


hpatch_inline static //check strongChecksumByteSize is strong enough?
bool getStrongForHashClash(size_t kSafeHashClashBit,hpatch_StreamPos_t newDataSize,uint32_t kMatchBlockSize,
                           size_t strongChecksumByteSize){
    if (strongChecksumByteSize<kStrongChecksumByteSize_min)
        return false;
    size_t needHashByte=sync_private::getNeedHashByte(kSafeHashClashBit,newDataSize,kMatchBlockSize);
    return sync_private::_kMaxRollHashByte+strongChecksumByteSize>=needHashByte;
}

hpatch_inline static
hpatch_StreamPos_t estimatePatchMemSize(size_t kSafeHashClashBit,hpatch_StreamPos_t newDataSize,
                                        uint32_t kMatchBlockSize,bool isUsedCompress){
    hpatch_StreamPos_t blockCount=getSyncBlockCount(newDataSize,kMatchBlockSize);
    hpatch_StreamPos_t bet=24;
    bet+= sync_private::getNeedHashByte(kSafeHashClashBit,newDataSize,kMatchBlockSize);
    bet+=isUsedCompress?4:0;
    return bet*blockCount + 2*(hpatch_StreamPos_t)kMatchBlockSize;
}

#endif // sync_patch_hash_clash_h
