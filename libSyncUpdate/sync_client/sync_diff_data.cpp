//  sync_diff_data.cpp
//  sync_client
//  Created by housisong on 2020-02-09.
/*
 The MIT License (MIT)
 Copyright (c) 2019-2020 HouSisong
 
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
#include "sync_diff_data.h"
#include <stdlib.h>
#include <vector>
#include <stdexcept>
#include "match_in_old.h"
#include "../../libHDiffPatch/HPatch/patch_private.h"
typedef unsigned char TByte;
namespace sync_private{

#define _pushV(vec,uv,stag) \
    do{ TByte _buf[hpatch_kMaxPackedUIntBytes]; \
        TByte* cur=_buf; \
        if (!hpatch_packUIntWithTag(&cur,_buf+sizeof(_buf),uv,stag,1)) \
            throw std::runtime_error("_packMatchedPoss() _pushV()"); \
        vec.insert(vec.end(),_buf,cur); \
    }while(0)

static const char* kSyncDiffType="HSyncDiff20";
static const size_t kMaxRleValue=63;

static void _packMatchedPoss(const hpatch_StreamPos_t* newBlockDataInOldPoss,uint32_t kBlockCount,
                             uint32_t kBlockSize,std::vector<TByte>& out_possBuf){
    hpatch_StreamPos_t backv=0;
    uint32_t back_i=kBlockCount;
    for (uint32_t i=0; i<kBlockCount; ++i) {
        hpatch_StreamPos_t v=newBlockDataInOldPoss[i];
        if (v!=kBlockType_needSync){
            if (v>=backv)
                _pushV(out_possBuf,(hpatch_StreamPos_t)(v-backv),1);
            else
                _pushV(out_possBuf,(hpatch_StreamPos_t)(backv-v)+kMaxRleValue,0);
            backv=v+kBlockSize;
        }else{
            if ((back_i+1==i)&&(out_possBuf.back()<kMaxRleValue))
                ++out_possBuf.back();  //needSync count==back+1
            else
                _pushV(out_possBuf,0,0);
            back_i=i;
        }
    }
}

bool _saveSyncDiffData(const hpatch_StreamPos_t* newBlockDataInOldPoss,uint32_t kBlockCount,uint32_t kBlockSize,
                       const hpatch_TStreamOutput* out_diffStream,hpatch_StreamPos_t* out_diffDataPos){
    try {
        std::vector<TByte> possBuf;
        _packMatchedPoss(newBlockDataInOldPoss,kBlockCount,kBlockSize,possBuf);
        
        std::vector<TByte> headBuf;
        headBuf.insert(headBuf.end(),kSyncDiffType,kSyncDiffType+strlen(kSyncDiffType)+1); //with '\0'
        _pushV(headBuf,kBlockCount,0);
        _pushV(headBuf,kBlockSize,0);
        _pushV(headBuf,possBuf.size(),0);
        if (!out_diffStream->write(out_diffStream,0,headBuf.data(),
                                   headBuf.data()+headBuf.size())) return false;
        if (!out_diffStream->write(out_diffStream,headBuf.size(),possBuf.data(),
                                   possBuf.data()+possBuf.size())) return false;
        *out_diffDataPos=(hpatch_StreamPos_t)headBuf.size()+possBuf.size();
        return true;
    } catch (...) {
        return false;
    }
}


    
TSyncDiffData::TSyncDiffData():in_diffStream(0){
    memset((IReadSyncDataListener*)this,0,sizeof(IReadSyncDataListener));
    memset(&this->localPoss,0,sizeof(this->localPoss));
}
TSyncDiffData::~TSyncDiffData(){
    if (this->localPoss.packedOldPoss)
        free((void*)this->localPoss.packedOldPoss);
}

    
static hpatch_BOOL _TSyncDiffData_readSyncData(IReadSyncDataListener* listener,uint32_t blockIndex,
                                               hpatch_StreamPos_t posInNewSyncData,
                                               uint32_t syncDataSize,unsigned char* out_syncDataBuf){
    TSyncDiffData* self=(TSyncDiffData*)listener->readSyncDataImport;
    hpatch_BOOL result=self->in_diffStream->read(self->in_diffStream,self->readedPos,
                                                 out_syncDataBuf,out_syncDataBuf+syncDataSize);
    self->readedPos+=syncDataSize;
    return result;
}
    
static hpatch_BOOL _loadPoss(TSyncDiffLocalPoss* self,const hpatch_TStreamInput* in_diffStream,
                             hpatch_StreamPos_t* out_diffDataPos){
    assert(self->packedOldPoss==0);
    hpatch_StreamPos_t  v;
    size_t              packedOldPossSize;
    TStreamCacheClip    clip;
    TByte               _cache[hpatch_kStreamCacheSize];
    _TStreamCacheClip_init(&clip,in_diffStream,*out_diffDataPos,in_diffStream->streamSize,
                           _cache,hpatch_kStreamCacheSize);
    { // type
        char saved_type[hpatch_kMaxPluginTypeLength+1];
        if (!_TStreamCacheClip_readType_end(&clip,'\0',saved_type)) return hpatch_FALSE;
        if (0!=strcmp(saved_type,kSyncDiffType)) return hpatch_FALSE;//unsupport type
    }
    { // kBlockCount
        if (!_TStreamCacheClip_unpackUIntWithTag(&clip,&v,1)) return hpatch_FALSE;
        if (v!=(uint32_t)v) return hpatch_FALSE;
        self->kBlockCount=(uint32_t)v;
    }
    { // kBlockSize
        if (!_TStreamCacheClip_unpackUIntWithTag(&clip,&v,1)) return hpatch_FALSE;
        if (v!=(uint32_t)v) return hpatch_FALSE;
        self->kBlockSize=(uint32_t)v;
    }
    { // packedOldPossSize
        if (!_TStreamCacheClip_unpackUIntWithTag(&clip,&v,1)) return hpatch_FALSE;
        if (v!=(size_t)v) return hpatch_FALSE;
        packedOldPossSize=(size_t)v;
    }
    {  // packedOldPoss data
        self->packedOldPoss=(TByte*)malloc(packedOldPossSize);
        if (!self->packedOldPoss) return hpatch_FALSE;
        self->packedOldPossEnd=self->packedOldPoss+packedOldPossSize;
        if (!_TStreamCacheClip_readDataTo(&clip,(TByte*)self->packedOldPoss,
                                          (TByte*)self->packedOldPossEnd)) return hpatch_FALSE;
    }
    *out_diffDataPos=_TStreamCacheClip_readPosOfSrcStream(&clip);
    return hpatch_TRUE;
}
    
hpatch_BOOL _syncDiffLocalPoss_nextOldPos(TSyncDiffLocalPoss* self,hpatch_StreamPos_t* out_oldPos){
    if (self->packedNeedSyncCount>0){
        --self->packedNeedSyncCount;
        *out_oldPos=kBlockType_needSync;
        return hpatch_TRUE;
    }
    if (self->packedOldPoss<self->packedOldPossEnd){
        hpatch_StreamPos_t v;
        size_t tag=(*self->packedOldPoss)>>7;
        if (!hpatch_unpackUIntWithTag(&self->packedOldPoss,self->packedOldPossEnd,&v,1)) return hpatch_FALSE;
        if (tag==0){
            if (v<=kMaxRleValue){
                self->packedNeedSyncCount=(uint32_t)v;
                *out_oldPos=kBlockType_needSync;
            }else{
                if (self->backPos<(v-kMaxRleValue)) return hpatch_FALSE;
                v=self->backPos-(v-kMaxRleValue);
                *out_oldPos=v;
                self->backPos=v+self->kBlockSize;
            }
        }else{ //tag==1
            v+=self->backPos;
            *out_oldPos=v;
            self->backPos=v+self->kBlockSize;
        }
        return hpatch_TRUE;
    }else{
        return hpatch_FALSE;
    }
}
    
static void _TSyncDiffData_openOldPoss(IReadSyncDataListener* listener,void* localPosHandle){
    TSyncDiffData* self=(TSyncDiffData*)listener->readSyncDataImport;
    TSyncDiffLocalPoss* localPoss=(TSyncDiffLocalPoss*)localPosHandle;
    *localPoss=self->localPoss;
}

bool _loadSyncDiffData(TSyncDiffData* self,const hpatch_TStreamInput* in_diffStream){
    assert(self->readSyncDataImport==0);
    assert(self->localPoss.packedOldPoss==0);
    self->readSyncDataImport=self;
    self->readSyncData=_TSyncDiffData_readSyncData;
    self->localPatch_openOldPoss=_TSyncDiffData_openOldPoss;
    self->in_diffStream=in_diffStream;
    self->readedPos=0;
    return 0!=_loadPoss(&self->localPoss,in_diffStream,&self->readedPos);
}

} //namespace sync_private
