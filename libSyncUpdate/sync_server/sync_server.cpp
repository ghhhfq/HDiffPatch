//  sync_server.cpp
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
#include "sync_server.h"
#include "sync_info_server.h"
#include <vector>
#include "match_in_new.h"
#include "../sync_client/mt_by_queue.h"
using namespace hdiff_private;
using namespace sync_private;

struct _TCreateDatas {
    const hpatch_TStreamInput*  newData;
    TNewDataSyncInfo*           out_newSyncInfo;
    const hpatch_TStreamOutput* out_newSyncData;
    const hdiff_TCompress*      compressPlugin;
    hpatch_StreamPos_t          curOutPos;
};

static void mt_create_sync_data(_TCreateDatas& cd,void* _mt=0,int threadIndex=0){
    TNewDataSyncInfo*       out_newSyncInfo=cd.out_newSyncInfo;
    const hdiff_TCompress*  compressPlugin=cd.compressPlugin;
    const uint32_t kMatchBlockSize=out_newSyncInfo->kMatchBlockSize;
    hpatch_TChecksum*       strongChecksumPlugin=out_newSyncInfo->_strongChecksumPlugin;
    const uint32_t kBlockCount=(uint32_t)getSyncBlockCount(out_newSyncInfo->newDataSize,kMatchBlockSize);
    std::vector<TByte> buf(kMatchBlockSize);
    std::vector<TByte> cmbuf(compressPlugin?((size_t)compressPlugin->maxCompressedSize(kMatchBlockSize)):0);
    const size_t checksumByteSize=strongChecksumPlugin->checksumByteSize();
    checkv((checksumByteSize==(uint32_t)checksumByteSize)
          &&(checksumByteSize>=kPartStrongChecksumByteSize)
          &&(checksumByteSize%kPartStrongChecksumByteSize==0));
    CChecksum checksumBlockData(strongChecksumPlugin,false);
#if (_IS_USED_MULTITHREAD)
    TMt_by_queue* mt=(TMt_by_queue*)_mt;
#endif
    
    hpatch_StreamPos_t curReadPos=0;
    for (uint32_t i=0; i<kBlockCount; ++i,curReadPos+=kMatchBlockSize) {
#if (_IS_USED_MULTITHREAD)
        if (mt&&(!mt->getWork(threadIndex,i))) continue; //next work;
#endif
        size_t dataLen=kMatchBlockSize;
        if (i==kBlockCount-1) dataLen=(size_t)(cd.newData->streamSize-curReadPos);
        {//read data
#if (_IS_USED_MULTITHREAD)
            TMt_by_queue::TAutoInputLocker _autoLocker(mt);
#endif
            checkv(cd.newData->read(cd.newData,curReadPos,buf.data(),buf.data()+dataLen));
        }
        size_t backZeroLen=kMatchBlockSize-dataLen;
        if (backZeroLen>0)
            memset(buf.data()+dataLen,0,backZeroLen);
        
        uint64_t rollHash;
        //roll hash
        if (out_newSyncInfo->is32Bit_rollHash)
            rollHash=roll_hash_start((uint32_t*)0,buf.data(),kMatchBlockSize);
        else
            rollHash=roll_hash_start((uint64_t*)0,buf.data(),kMatchBlockSize);
        //strong hash
        checksumBlockData.appendBegin();
        checksumBlockData.append(buf.data(),buf.data()+kMatchBlockSize);
        checksumBlockData.appendEnd();
        //compress
        size_t compressedSize=0;
        if (compressPlugin){
            compressedSize=hdiff_compress_mem(compressPlugin,cmbuf.data(),cmbuf.data()+cmbuf.size(),
                                              buf.data(),buf.data()+kMatchBlockSize);
            checkv(compressedSize>0);
            if (compressedSize+sizeof(uint32_t)>=kMatchBlockSize)
                compressedSize=0; //not compressed
            //save compressed size
            checkv(compressedSize==(uint32_t)compressedSize);
        }
        {//save data
#if (_IS_USED_MULTITHREAD)
            TMt_by_queue::TAutoQueueLocker _autoLocker(mt?&mt->outputQueue:0,threadIndex,i);
            checkv(_autoLocker.isWaitOk);
#endif
            if (out_newSyncInfo->is32Bit_rollHash)
                ((uint32_t*)out_newSyncInfo->rollHashs)[i]=(uint32_t)rollHash;
            else
                ((uint64_t*)out_newSyncInfo->rollHashs)[i]=rollHash;
            
            checkChecksumAppendData(cd.out_newSyncInfo->newDataCheckChecksum,i,
                                    checksumBlockData.checksum.data(),checksumByteSize);
            toSyncPartChecksum(checksumBlockData.checksum.data(),
                               checksumBlockData.checksum.data(),checksumByteSize);
            memcpy(out_newSyncInfo->partChecksums+i*(size_t)kPartStrongChecksumByteSize,
                   checksumBlockData.checksum.data(),kPartStrongChecksumByteSize);
            if (compressPlugin){
                if (compressedSize>0)
                    out_newSyncInfo->savedSizes[i]=(uint32_t)compressedSize;
                else
                    out_newSyncInfo->savedSizes[i]=kMatchBlockSize;
            }
            
            if (compressedSize>0){
                if (cd.out_newSyncData)
                    writeStream(cd.out_newSyncData,cd.curOutPos, cmbuf.data(),compressedSize);
                out_newSyncInfo->newSyncDataSize+=compressedSize;
            }else{
                if (cd.out_newSyncData)
                    writeStream(cd.out_newSyncData,cd.curOutPos, buf.data(),dataLen); //!
                out_newSyncInfo->newSyncDataSize+=dataLen;
            }
        }
    }
}

#if (_IS_USED_MULTITHREAD)
struct TMt_threadDatas{
    _TCreateDatas*      createDatas;
    TMt_by_queue*       shareDatas;
};

static void _mt_threadRunCallBackProc(int threadIndex,void* workData){
    TMt_threadDatas* tdatas=(TMt_threadDatas*)workData;
    mt_create_sync_data(*tdatas->createDatas,tdatas->shareDatas,threadIndex);
    tdatas->shareDatas->finish();
    bool isMainThread=(threadIndex==tdatas->shareDatas->threadNum-1);
    if (isMainThread) tdatas->shareDatas->waitAllFinish();
}
#endif

void _private_create_sync_data(TNewDataSyncInfo*           newSyncInfo,
                               const hpatch_TStreamInput*  newData,
                               const hpatch_TStreamOutput* out_newSyncInfo,
                               const hpatch_TStreamOutput* out_newSyncData,
                               const hdiff_TCompress* compressPlugin,size_t threadNum){
    _TCreateDatas  createDatas;
    createDatas.newData=newData;
    createDatas.out_newSyncInfo=newSyncInfo;
    createDatas.out_newSyncData=out_newSyncData;
    createDatas.compressPlugin=compressPlugin;
    createDatas.curOutPos=0;

    const uint32_t kMatchBlockSize=createDatas.out_newSyncInfo->kMatchBlockSize;
    checkv(kMatchBlockSize>=kMatchBlockSize_min);
    if (createDatas.compressPlugin) checkv(createDatas.out_newSyncData!=0);
    
    checkChecksumInit(createDatas.out_newSyncInfo->newDataCheckChecksum,
                      createDatas.out_newSyncInfo->kStrongChecksumByteSize);
    
#if (_IS_USED_MULTITHREAD)
    if (threadNum>1){
        TMt_by_queue   shareDatas((int)threadNum,true);
        TMt_threadDatas  tdatas;  memset(&tdatas,0,sizeof(tdatas));
        tdatas.shareDatas=&shareDatas;
        tdatas.createDatas=&createDatas;
        thread_parallel((int)threadNum,_mt_threadRunCallBackProc,&tdatas,1);
    }else
#endif
    {
        mt_create_sync_data(createDatas);
    }
    checkChecksumEnd(createDatas.out_newSyncInfo->newDataCheckChecksum,
                      createDatas.out_newSyncInfo->kStrongChecksumByteSize);
    matchNewDataInNew(createDatas.out_newSyncInfo);
    //save to out_newSyncInfo_stream
    TNewDataSyncInfo_saveTo(newSyncInfo,out_newSyncInfo,compressPlugin);
}

void create_sync_data(const hpatch_TStreamInput*  newData,
                      const hpatch_TStreamOutput* out_newSyncInfo,
                      const hpatch_TStreamOutput* out_newSyncData,
                      const hdiff_TCompress* compressPlugin,
                      hpatch_TChecksum*      strongChecksumPlugin,
                      uint32_t kMatchBlockSize,size_t threadNum,
                      const unsigned char* externData_begin,const unsigned char* externData_end){
    assert(externData_begin<=externData_end);
    CNewDataSyncInfo newSyncInfo(strongChecksumPlugin,compressPlugin,
                                 newData->streamSize,kMatchBlockSize);
    newSyncInfo.externData_begin=externData_begin;
    newSyncInfo.externData_end=externData_end;
    _private_create_sync_data(&newSyncInfo,newData,out_newSyncInfo,out_newSyncData,compressPlugin,threadNum);
}

void create_sync_data_by_file(const char* newDataFile,
                              const char* out_hsyni_file,
                              const char* out_hsynd_file,
                              const hdiff_TCompress* compressPlugin,
                              hpatch_TChecksum*      strongChecksumPlugin,
                              uint32_t kMatchBlockSize,size_t threadNum,
                              const unsigned char* externData_begin,const unsigned char* externData_end){
    CFileStreamInput  newData(newDataFile);
    CFileStreamOutput out_newSyncInfo(out_hsyni_file,~(hpatch_StreamPos_t)0);
    CFileStreamOutput out_newSyncData;
    const hpatch_TStreamOutput* newDataStream=0;
    if (out_hsynd_file){
        out_newSyncData.open(out_hsynd_file,~(hpatch_StreamPos_t)0);
        newDataStream=&out_newSyncData.base;
    }
    
    create_sync_data(&newData.base,&out_newSyncInfo.base,newDataStream,compressPlugin,
                     strongChecksumPlugin,kMatchBlockSize,threadNum,externData_begin,externData_end);
}

void create_sync_data_by_file(const char* newDataFile,
                              const char* out_hsyni_file,
                              hpatch_TChecksum*      strongChecksumPlugin,
                              uint32_t kMatchBlockSize,size_t threadNum,
                              const unsigned char* externData_begin,const unsigned char* externData_end){
    create_sync_data_by_file(newDataFile,out_hsyni_file,0,0, strongChecksumPlugin,
                             kMatchBlockSize,threadNum,externData_begin,externData_end);
}

void create_sync_data(const hpatch_TStreamInput*  newData,
                      const hpatch_TStreamOutput* out_newSyncInfo, //newSyncData same as newData
                      hpatch_TChecksum*      strongChecksumPlugin,
                      uint32_t kMatchBlockSize,size_t threadNum,
                      const unsigned char* externData_begin,const unsigned char* externData_end){
    create_sync_data(newData,out_newSyncInfo,0,0, strongChecksumPlugin,
                     kMatchBlockSize,threadNum,externData_begin,externData_end);
}
