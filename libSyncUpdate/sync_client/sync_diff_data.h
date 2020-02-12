//  sync_diff_data.h
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
#ifndef sync_diff_data_h
#define sync_diff_data_h
#include "sync_client_type.h"
namespace sync_private{

bool _saveSyncDiffData(const hpatch_StreamPos_t* newBlockDataInOldPoss,uint32_t kBlockCount,uint32_t kBlockSize,
                       const hpatch_TStreamOutput* out_diffStream,hpatch_StreamPos_t* out_diffDataPos);

    struct TSyncDiffLocalPoss{
        const unsigned char*  packedOldPoss;
        const unsigned char*  packedOldPossEnd;
        hpatch_StreamPos_t    backPos;
        uint32_t              packedNeedSyncCount;
        uint32_t              kBlockCount;
        uint32_t              kBlockSize;
    };
hpatch_BOOL _syncDiffLocalPoss_nextOldPos(TSyncDiffLocalPoss* self,hpatch_StreamPos_t* out_oldPos);
static hpatch_inline hpatch_BOOL _syncDiffLocalPoss_isFinish(const TSyncDiffLocalPoss* self){
        return (self->packedNeedSyncCount==0)&&(self->packedOldPoss==self->packedOldPossEnd); }
    
    struct TSyncDiffData:public IReadSyncDataListener{
        const hpatch_TStreamInput*  in_diffStream;
        TSyncDiffLocalPoss          localPoss;
        hpatch_StreamPos_t          readedPos;
        TSyncDiffData();
        ~TSyncDiffData();
    };
bool _loadSyncDiffData(TSyncDiffData* self,const hpatch_TStreamInput* in_diffStream);

}
#endif // sync_diff_data_h
