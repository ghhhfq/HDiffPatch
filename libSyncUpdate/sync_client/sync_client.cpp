//  sync_client.cpp
//  sync_client
//  Created by housisong on 2019-09-18.
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
#include "sync_client_private.h"
#include "../../file_for_patch.h"
#include "match_in_old.h"
#include "mt_by_queue.h"
#include <stdexcept>
namespace sync_private{

#define check(v,errorCode) \
            do{ if (!(v)) { if (result==kSyncClient_ok) result=errorCode; \
                            if (!_inClear) goto clear; } }while(0)

struct _TWriteDatas {
    const hpatch_TStreamOutput* out_newStream;
    const hpatch_TStreamOutput* out_diffStream;
    hpatch_StreamPos_t          outDiffDataPos;
    const hpatch_TStreamInput*  oldStream;
    const TNewDataSyncInfo*     newSyncInfo;
    const hpatch_StreamPos_t*   newBlockDataInOldPoss;
    uint32_t                    needSyncBlockCount;
    hpatch_TDecompress*         decompressPlugin;
    hpatch_TChecksum*           strongChecksumPlugin;
    IReadSyncDataListener*      syncDataListener;
};

static int mt_writeToNew(_TWriteDatas& wd,void* _mt=0,int threadIndex=0) {
    const TNewDataSyncInfo* newSyncInfo=wd.newSyncInfo;
    IReadSyncDataListener*  syncDataListener=wd.syncDataListener;
    hpatch_TChecksum*       strongChecksumPlugin=wd.strongChecksumPlugin;
    int result=kSyncClient_ok;
    int _inClear=0;
    const uint32_t kBlockCount=(uint32_t)TNewDataSyncInfo_blockCount(newSyncInfo);
    const uint32_t kMatchBlockSize=newSyncInfo->kMatchBlockSize;
    TByte*             dataBuf=0;
    TByte*             checksumSync_buf=0;
    hpatch_checksumHandle checksumSync=0;
    hpatch_StreamPos_t posInNewSyncData=0;
    hpatch_StreamPos_t outNewDataPos=0;
    hpatch_StreamPos_t outDiffDataPos=wd.outDiffDataPos;
    const hpatch_StreamPos_t oldDataSize=wd.oldStream->streamSize;
#if (_IS_USED_MULTITHREAD)
    TMt_by_queue* mt=(TMt_by_queue*)_mt;
#endif
    size_t _memSize=kMatchBlockSize*(wd.decompressPlugin?2:1)
                        +newSyncInfo->kStrongChecksumByteSize
                        +checkChecksumBufByteSize(newSyncInfo->kStrongChecksumByteSize);
    dataBuf=(TByte*)malloc(_memSize);
    check(dataBuf!=0,kSyncClient_memError);
    {//checksum newSyncData
        checksumSync_buf=dataBuf+_memSize-(newSyncInfo->kStrongChecksumByteSize
                                           +checkChecksumBufByteSize(newSyncInfo->kStrongChecksumByteSize));
        checksumSync=strongChecksumPlugin->open(strongChecksumPlugin);
        check(checksumSync!=0,kSyncClient_strongChecksumOpenError);
    }
    for (uint32_t syncSize,newDataSize,isNeedSync,cur_sync_i=0,i=0; i<kBlockCount; ++i,
                    outNewDataPos+=newDataSize,posInNewSyncData+=syncSize,cur_sync_i+=isNeedSync){
        syncSize=TNewDataSyncInfo_syncBlockSize(newSyncInfo,i);
        newDataSize=TNewDataSyncInfo_newDataBlockSize(newSyncInfo,i);
        const hpatch_StreamPos_t curSyncPos=wd.newBlockDataInOldPoss[i];
        isNeedSync=(curSyncPos==kBlockType_needSync)?1:0;
#if (_IS_USED_MULTITHREAD)
        size_t sync_i=isNeedSync?cur_sync_i:~(size_t)0;
        if (mt&&(!mt->getWork(threadIndex,i,sync_i))) continue; //next work;
#endif
        if (isNeedSync){
            TByte* buf=(syncSize<newDataSize)?(dataBuf+kMatchBlockSize):dataBuf;
            if ((wd.out_newStream)||(wd.out_diffStream)){//download data
#if (_IS_USED_MULTITHREAD)
                TMt_by_queue::TAutoQueueLocker _autoLocker(mt?&mt->inputQueue:0,threadIndex,sync_i);
                check(_autoLocker.isWaitOk,kSyncClient_readSyncDataError);
#endif
                check(syncDataListener->readSyncData(syncDataListener,
                                                     i,posInNewSyncData,syncSize,buf),
                      kSyncClient_readSyncDataError);
                if (wd.out_diffStream){ //out diff
                    check(wd.out_diffStream->write(wd.out_diffStream,outDiffDataPos,
                                                   buf,buf+syncSize),kSyncClient_saveDiffError);
                    outDiffDataPos+=syncSize;
                }
            }
            if (wd.out_newStream){
                if (syncSize<newDataSize){// need deccompress?
                    check(hpatch_deccompress_mem(wd.decompressPlugin,buf,buf+syncSize,
                                                 dataBuf,dataBuf+newDataSize),kSyncClient_decompressError);
                }
                { //checksum
                    if (newDataSize<kMatchBlockSize)//for backZeroLen
                        memset(dataBuf+newDataSize,0,kMatchBlockSize-newDataSize);
                    strongChecksumPlugin->begin(checksumSync);
                    strongChecksumPlugin->append(checksumSync,dataBuf,dataBuf+kMatchBlockSize);
                    strongChecksumPlugin->end(checksumSync,checksumSync_buf+kPartStrongChecksumByteSize,
                            checksumSync_buf+kPartStrongChecksumByteSize+newSyncInfo->kStrongChecksumByteSize);
                    toSyncPartChecksum(checksumSync_buf,checksumSync_buf+kPartStrongChecksumByteSize,
                                       newSyncInfo->kStrongChecksumByteSize);
                    check(0==memcmp(checksumSync_buf,
                                    newSyncInfo->partChecksums+i*(size_t)kPartStrongChecksumByteSize,
                                    kPartStrongChecksumByteSize),kSyncClient_checksumSyncDataError);
                }
            }
        }else{//copy from old
            assert(curSyncPos<oldDataSize);
            if (wd.out_newStream){
#if (_IS_USED_MULTITHREAD)
                TMt_by_queue::TAutoInputLocker _autoLocker(mt);
#endif
                check(wd.oldStream->read(wd.oldStream,curSyncPos,dataBuf,dataBuf+newDataSize),
                      kSyncClient_readOldDataError);
            }
        }
        if (wd.out_newStream){//write
#if (_IS_USED_MULTITHREAD)
            TMt_by_queue::TAutoQueueLocker _autoLocker(mt?&mt->outputQueue:0,threadIndex,i);
            check(_autoLocker.isWaitOk,kSyncClient_writeNewDataError);
#endif
            if (isNeedSync)
                checkChecksumAppendData(newSyncInfo->newDataCheckChecksum, i,
                                        checksumSync_buf+kPartStrongChecksumByteSize,
                                        newSyncInfo->kStrongChecksumByteSize);
            check(wd.out_newStream->write(wd.out_newStream,outNewDataPos,dataBuf,
                                          dataBuf+newDataSize), kSyncClient_writeNewDataError);
        }
    }
    assert(outNewDataPos==newSyncInfo->newDataSize);
    assert(posInNewSyncData==newSyncInfo->newSyncDataSize);
clear:
    _inClear=1;
#if (_IS_USED_MULTITHREAD)
    if ((result!=kSyncClient_ok)&&(mt))
        mt->stop();
#endif
    if (checksumSync) strongChecksumPlugin->close(strongChecksumPlugin,checksumSync);
    if (dataBuf) free(dataBuf);
    return result;
}


#if (_IS_USED_MULTITHREAD)
struct TMt_threadDatas{
    _TWriteDatas*       writeDatas;
    TMt_by_queue*       shareDatas;
    int                 result;
};

static void _mt_threadRunCallBackProc(int threadIndex,void* workData){
    TMt_threadDatas* tdatas=(TMt_threadDatas*)workData;
    int result=mt_writeToNew(*tdatas->writeDatas,tdatas->shareDatas,threadIndex);
    {//set result
        TMt_by_queue::TAutoLocker _auto_locker(tdatas->shareDatas);
        if (tdatas->result==kSyncClient_ok) tdatas->result=result;
    }
    tdatas->shareDatas->finish();
    bool isMainThread=(threadIndex==tdatas->shareDatas->threadNum-1);
    if (isMainThread) tdatas->shareDatas->waitAllFinish();
}
#endif

static int writeToNew(_TWriteDatas& writeDatas,int threadNum) {

#if (_IS_USED_MULTITHREAD)
    if (threadNum>1){
        const uint32_t kBlockCount=(uint32_t)TNewDataSyncInfo_blockCount(writeDatas.newSyncInfo);
        TMt_by_queue   shareDatas((int)threadNum,kBlockCount,writeDatas.out_newStream!=0,
                                  writeDatas.needSyncBlockCount);
        TMt_threadDatas  tdatas;
        tdatas.shareDatas=&shareDatas;
        tdatas.writeDatas=&writeDatas;
        tdatas.result=kSyncClient_ok;
        thread_parallel((int)threadNum,_mt_threadRunCallBackProc,&tdatas,1);
        return tdatas.result;
    }else
#endif
    {
        return mt_writeToNew(writeDatas);
    }
}

    struct TNeedSyncInfosImport:public TNeedSyncInfos{
        const hpatch_StreamPos_t* newBlockDataInOldPoss;
    };
static void _getBlockInfoByIndex(const TNeedSyncInfos* needSyncInfos,uint32_t blockIndex,
                                 hpatch_BOOL* out_isNeedSync,uint32_t* out_syncSize){
    const TNeedSyncInfosImport* self=(const TNeedSyncInfosImport*)needSyncInfos->needSyncInfosImport;
    assert(blockIndex<self->blockCount);
    *out_isNeedSync=(self->newBlockDataInOldPoss[blockIndex]==kBlockType_needSync);
    *out_syncSize=TNewDataSyncInfo_syncBlockSize(self->newSyncInfo,blockIndex);
}
    
static void getNeedSyncInfo(const hpatch_StreamPos_t* newBlockDataInOldPoss,
                            const TNewDataSyncInfo* newSyncInfo,TNeedSyncInfosImport* out_nsi){
    const uint32_t kBlockCount=(uint32_t)TNewDataSyncInfo_blockCount(newSyncInfo);
    out_nsi->needSyncInfosImport=out_nsi; //self
    out_nsi->newBlockDataInOldPoss=newBlockDataInOldPoss;
    
    out_nsi->newSyncInfo=newSyncInfo;
    out_nsi->getBlockInfoByIndex=_getBlockInfoByIndex;
    out_nsi->blockCount=kBlockCount;
    out_nsi->needSyncBlockCount=0;
    out_nsi->needSyncSumSize=0;
    for (uint32_t i=0; i<kBlockCount; ++i){
        if (newBlockDataInOldPoss[i]==kBlockType_needSync){
            ++out_nsi->needSyncBlockCount;
            out_nsi->needSyncSumSize+=TNewDataSyncInfo_syncBlockSize(newSyncInfo,i);
        }
    }
}

#define _pushV(vec,uv,stag) \
    do{ TByte _buf[hpatch_kMaxPackedUIntBytes]; \
        TByte* cur=_buf; \
        if (!hpatch_packUIntWithTag(&cur,_buf+sizeof(_buf),uv,stag,1)) \
            throw std::runtime_error("_packMatchedPoss() _pushV()"); \
        vec.insert(vec.end(),_buf,cur); \
    }while(0)
static void _packMatchedPoss(const hpatch_StreamPos_t* newBlockDataInOldPoss,uint32_t kBlockCount,
                             std::vector<TByte>& out_possBuf){
    const size_t kMaxRle=127;
    hpatch_StreamPos_t backv=0;
    uint32_t back_i=kBlockCount;
    for (uint32_t i=0; i<kBlockCount; ++i) {
        hpatch_StreamPos_t v=newBlockDataInOldPoss[i];
        if (v!=kBlockType_needSync){
            if (v>=backv)
                _pushV(out_possBuf,(hpatch_StreamPos_t)(v-backv),1);
            else
                _pushV(out_possBuf,(hpatch_StreamPos_t)(backv-v)+kMaxRle,0);
            backv=v;
        }else{
            if ((back_i+1==i)&&(out_possBuf.back()<kMaxRle))
                ++out_possBuf.back();  //mean needSync count-1
            else
                _pushV(out_possBuf,0,0);
            back_i=i;
        }
    }
}

static bool saveDiffData(const hpatch_StreamPos_t* newBlockDataInOldPoss,uint32_t kBlockCount,
                         const hpatch_TStreamOutput* out_diffStream,hpatch_StreamPos_t* outDiffDataPos){
    const char* kSyncDiffType="HSyncDiff20";
    try {
        std::vector<TByte> possBuf;
        _packMatchedPoss(newBlockDataInOldPoss,kBlockCount,possBuf);
        
        std::vector<TByte> headBuf;
        headBuf.insert(headBuf.end(),kSyncDiffType,kSyncDiffType+strlen(kSyncDiffType)+1); //with '\0'
        _pushV(headBuf,kBlockCount,0);
        _pushV(headBuf,possBuf.size(),0);
        if (!out_diffStream->write(out_diffStream,0,headBuf.data(),
                                   headBuf.data()+headBuf.size())) return false;
        if (!out_diffStream->write(out_diffStream,headBuf.size(),possBuf.data(),
                                   possBuf.data()+possBuf.size())) return false;
        *outDiffDataPos=(hpatch_StreamPos_t)headBuf.size()+possBuf.size();
        return true;
    } catch (...) {
        return false;
    }
}

int _sync_patch(ISyncInfoListener* listener,IReadSyncDataListener* syncDataListener,
                const hpatch_TStreamInput* oldStream,const TNewDataSyncInfo* newSyncInfo,
                const hpatch_TStreamOutput* out_newStream,const hpatch_TStreamOutput* out_diffStream,int threadNum){
    assert(listener!=0);
    hpatch_TDecompress* decompressPlugin=0;
    hpatch_TChecksum*   strongChecksumPlugin=0;
    const uint32_t kBlockCount=(uint32_t)TNewDataSyncInfo_blockCount(newSyncInfo);
    TNeedSyncInfosImport needSyncInfo; memset(&needSyncInfo,0,sizeof(needSyncInfo));
    hpatch_StreamPos_t* newBlockDataInOldPoss=0;
    hpatch_StreamPos_t  outDiffDataPos=0;
    int result=kSyncClient_ok;
    int _inClear=0;
    
    //decompressPlugin
    if (newSyncInfo->compressType){
        if ((newSyncInfo->_decompressPlugin!=0)
            &&(newSyncInfo->_decompressPlugin->is_can_open(newSyncInfo->compressType))){
            decompressPlugin=newSyncInfo->_decompressPlugin;
        }else{
            decompressPlugin=listener->findDecompressPlugin(listener,newSyncInfo->compressType);
            check(decompressPlugin!=0,kSyncClient_noDecompressPluginError);
        }
    }
    //strongChecksumPlugin
    if ((newSyncInfo->_strongChecksumPlugin!=0)
        &&(newSyncInfo->kStrongChecksumByteSize==newSyncInfo->_strongChecksumPlugin->checksumByteSize())
        &&(0==strcmp(newSyncInfo->strongChecksumType,newSyncInfo->_strongChecksumPlugin->checksumType()))){
        strongChecksumPlugin=newSyncInfo->_strongChecksumPlugin;
    }else{
        strongChecksumPlugin=listener->findChecksumPlugin(listener,newSyncInfo->strongChecksumType);
        check(strongChecksumPlugin!=0,kSyncClient_noStrongChecksumPluginError);
        check(strongChecksumPlugin->checksumByteSize()==newSyncInfo->kStrongChecksumByteSize,
              kSyncClient_strongChecksumByteSizeError);
    }

    checkChecksumInit(newSyncInfo->newDataCheckChecksum,newSyncInfo->kStrongChecksumByteSize);
    //match in oldData
    newBlockDataInOldPoss=(hpatch_StreamPos_t*)malloc(kBlockCount*(size_t)sizeof(hpatch_StreamPos_t));
    check(newBlockDataInOldPoss!=0,kSyncClient_memError);
    try{
        matchNewDataInOld(newBlockDataInOldPoss,newSyncInfo,oldStream,strongChecksumPlugin,threadNum);
    }catch(const std::exception& e){
        fprintf(stderr,"matchNewDataInOld() run an error: %s",e.what());
        result=kSyncClient_matchNewDataInOldError;
    }
    check(result==kSyncClient_ok,result);
    getNeedSyncInfo(newBlockDataInOldPoss,newSyncInfo,&needSyncInfo);
    
    
    if (out_diffStream){
        check(saveDiffData(newBlockDataInOldPoss,kBlockCount,
                           out_diffStream,&outDiffDataPos),kSyncClient_saveDiffError);
    }
    
    if (listener->needSyncInfo)
        listener->needSyncInfo(listener,&needSyncInfo);
    
    if (syncDataListener->readSyncDataBegin)
        check(syncDataListener->readSyncDataBegin(syncDataListener,&needSyncInfo),
              kSyncClient_readSyncDataBeginError);
    if (out_newStream||out_diffStream){
        _TWriteDatas writeDatas;
        writeDatas.out_newStream=out_newStream;
        writeDatas.out_diffStream=out_diffStream;
        writeDatas.outDiffDataPos=outDiffDataPos;
        writeDatas.oldStream=oldStream;
        writeDatas.newSyncInfo=newSyncInfo;
        writeDatas.newBlockDataInOldPoss=newBlockDataInOldPoss;
        writeDatas.needSyncBlockCount=needSyncInfo.needSyncBlockCount;
        writeDatas.decompressPlugin=decompressPlugin;
        writeDatas.strongChecksumPlugin=strongChecksumPlugin;
        writeDatas.syncDataListener=syncDataListener;
        result=writeToNew(writeDatas,threadNum);
        
        if ((result==kSyncClient_ok)&&out_newStream){
            checkChecksumEndTo(newSyncInfo->newDataCheckChecksum+newSyncInfo->kStrongChecksumByteSize,
                               newSyncInfo->newDataCheckChecksum,newSyncInfo->kStrongChecksumByteSize);
            check(0==memcmp(newSyncInfo->newDataCheckChecksum+newSyncInfo->kStrongChecksumByteSize,
                            newSyncInfo->newDataCheckChecksum,newSyncInfo->kStrongChecksumByteSize),
                  kSyncClient_newDataCheckChecksumError);
        }
    }
    
    if (syncDataListener->readSyncDataEnd)
        syncDataListener->readSyncDataEnd(syncDataListener);
clear:
    _inClear=1;
    if (newBlockDataInOldPoss) free(newBlockDataInOldPoss);
    return result;
}

static hpatch_BOOL _TSyncDiffData_readSyncData(IReadSyncDataListener* listener,uint32_t blockIndex,
                                               hpatch_StreamPos_t posInNewSyncData,
                                               uint32_t syncDataSize,unsigned char* out_syncDataBuf){
    TSyncDiffData* self=(TSyncDiffData*)listener->readSyncDataImport;
    if (posInNewSyncData < self->inNewPosEnd){ assert(false); return hpatch_FALSE; }
    hpatch_BOOL result=self->in_diffStream->read(self->in_diffStream,self->readedPos,
                                                 out_syncDataBuf,out_syncDataBuf+syncDataSize);
    self->readedPos+=syncDataSize;
    self->inNewPosEnd=posInNewSyncData+syncDataSize;
    return result;
}
void _initSyncDiffData(TSyncDiffData* diffData,const hpatch_TStreamInput* in_diffStream){
    memset(diffData,0,sizeof(*diffData));
    diffData->readSyncDataImport=diffData;
    diffData->readSyncData=_TSyncDiffData_readSyncData;
    diffData->in_diffStream=in_diffStream;
    diffData->readedPos=0;
    diffData->inNewPosEnd=0;
}

static int _sync_patch_file2file(ISyncInfoListener* listener,IReadSyncDataListener* syncDataListener,
                                 const char* oldFile,const char* newSyncInfoFile,
                                 const char* outNewFile,const hpatch_TStreamOutput* out_diffStream,int threadNum){
    int result=kSyncClient_ok;
    int _inClear=0;
    TNewDataSyncInfo         newSyncInfo;
    hpatch_TFileStreamInput  oldData;
    hpatch_TFileStreamOutput out_newData;
    const hpatch_TStreamInput* oldStream=0;
    bool isOldPathInputEmpty=(oldFile==0)||(strlen(oldFile)==0);
    
    TNewDataSyncInfo_init(&newSyncInfo);
    hpatch_TFileStreamInput_init(&oldData);
    hpatch_TFileStreamOutput_init(&out_newData);
    result=TNewDataSyncInfo_open_by_file(&newSyncInfo,newSyncInfoFile,listener);
    check(result==kSyncClient_ok,result);
    
    if (!isOldPathInputEmpty)
        check(hpatch_TFileStreamInput_open(&oldData,oldFile),kSyncClient_oldFileOpenError);
    oldStream=&oldData.base;
    if (outNewFile)
        check(hpatch_TFileStreamOutput_open(&out_newData,outNewFile,(hpatch_StreamPos_t)(-1)),
              kSyncClient_newFileCreateError);
    
    result=_sync_patch(listener,syncDataListener,oldStream,&newSyncInfo,
                       outNewFile?&out_newData.base:0,out_diffStream,threadNum);
clear:
    _inClear=1;
    check(hpatch_TFileStreamOutput_close(&out_newData),kSyncClient_newFileCloseError);
    check(hpatch_TFileStreamInput_close(&oldData),kSyncClient_oldFileCloseError);
    TNewDataSyncInfo_close(&newSyncInfo);
    return result;
}

} //namespace sync_private
using namespace  sync_private;

int sync_patch(ISyncInfoListener* listener,IReadSyncDataListener* syncDataListener,
               const hpatch_TStreamInput* oldStream,const TNewDataSyncInfo* newSyncInfo,
               const hpatch_TStreamOutput* out_newStream,int threadNum){
    return _sync_patch(listener,syncDataListener,oldStream,newSyncInfo,out_newStream,0,threadNum);
}

int sync_local_diff(ISyncInfoListener* listener,IReadSyncDataListener* syncDataListener,
                    const hpatch_TStreamInput* oldStream,const TNewDataSyncInfo* newSyncInfo,
                    const hpatch_TStreamOutput* out_diffStream,int threadNum){
    return _sync_patch(listener,syncDataListener,oldStream,newSyncInfo,0,out_diffStream,threadNum);
}


int sync_local_patch(ISyncInfoListener* listener,const hpatch_TStreamInput* in_diffStream,
                     const hpatch_TStreamInput* oldStream,const TNewDataSyncInfo* newSyncInfo,
                     const hpatch_TStreamOutput* out_newStream,int threadNum){
    TSyncDiffData diffData; _initSyncDiffData(&diffData,in_diffStream);
    return _sync_patch(listener,&diffData,oldStream,newSyncInfo,out_newStream,0,threadNum);
}


int sync_patch_file2file(ISyncInfoListener* listener,IReadSyncDataListener* syncDataListener,
                         const char* oldFile,const char* newSyncInfoFile,const char* outNewFile,int threadNum){
    return _sync_patch_file2file(listener,syncDataListener,oldFile,newSyncInfoFile,outNewFile,0,threadNum);
}


int sync_local_diff_file2file(ISyncInfoListener* listener,IReadSyncDataListener* syncDataListener,
                              const char* oldFile,const char* newSyncInfoFile,const char* outDiffFile,int threadNum){
    int result=kSyncClient_ok;
    int _inClear=0;
    hpatch_TFileStreamOutput out_diffData;
    hpatch_TFileStreamOutput_init(&out_diffData);
    check(hpatch_TFileStreamOutput_open(&out_diffData,outDiffFile,(hpatch_StreamPos_t)(-1)),
          kSyncClient_diffFileCreateError);
    result=_sync_patch_file2file(listener,syncDataListener,oldFile,newSyncInfoFile,0,&out_diffData.base,threadNum);
clear:
    _inClear=1;
    check(hpatch_TFileStreamOutput_close(&out_diffData),kSyncClient_diffFileCloseError);
    return result;
}

int sync_local_patch_file2file(ISyncInfoListener* listener,const char* inDiffFile,
                               const char* oldFile,const char* newSyncInfoFile,const char* outNewFile,int threadNum){
    int result=kSyncClient_ok;
    int _inClear=0;
    TSyncDiffData diffData;
    hpatch_TFileStreamInput in_diffData;
    hpatch_TFileStreamInput_init(&in_diffData);
    check(hpatch_TFileStreamInput_open(&in_diffData,inDiffFile),
          kSyncClient_diffFileOpenError);
    _initSyncDiffData(&diffData,&in_diffData.base);
    result=_sync_patch_file2file(listener,&diffData,oldFile,newSyncInfoFile,outNewFile,0,threadNum);
clear:
    _inClear=1;
    check(hpatch_TFileStreamInput_close(&in_diffData),kSyncClient_diffFileCloseError);
    return result;
}
