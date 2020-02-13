//  client_download_http.cpp
//  sync_client
//  Created by housisong on 2020-01-29.
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
#include "client_download_http.h"
#include <assert.h>
#include <vector>
#include "../../file_for_patch.h"
#include "../../libParallel/parallel_import.h" //this_thread_yield
#include "../../libHDiffPatch/HDiff/private_diff/mem_buf.h"
using namespace hdiff_private;

#ifndef _IsNeedIncludeDefaultMiniHttpHead
#   define _IsNeedIncludeDefaultMiniHttpHead 1
#endif

#if (_IsNeedIncludeDefaultMiniHttpHead)
#   include "minihttp.h" // https://github.com/sisong/minihttp
#endif
using namespace minihttp;
#ifdef WIN32
#   pragma comment(lib, "wsock32")
#   pragma comment(lib, "Ws2_32.lib")
#endif

bool doInitNetwork(){
    struct TInitNetwork {
        TInitNetwork():initOk(false){ initOk=InitNetwork(); }
        ~TInitNetwork(){ if (initOk) StopNetwork(); }
        bool initOk;
    };
    static TInitNetwork nt;
    return nt.initOk;
}

const size_t kBestBufSize=64*(1<<10);
const size_t kStepMaxCacheSize=512*(1<<10);
const size_t kStepMaxRangCount=64;
const char*  kHttpUserAgent="hsync" HDIFFPATCH_VERSION_STRING;

struct THttpDownload:public HttpSocket{
    explicit THttpDownload(const hpatch_TStreamOutput* out_stream,
                           hpatch_StreamPos_t curOutPos)
    :is_finished(false),is_write_error(false),_out_stream(out_stream),_cur_pos(curOutPos){}
    volatile bool   is_finished;
    volatile bool   is_write_error;
    
    const hpatch_TStreamOutput* _out_stream;
    hpatch_StreamPos_t          _cur_pos;
    virtual void _OnRequestDone(){
        is_finished = true;
    }
    virtual void _OnRecv(void *incoming, unsigned size){
        if(!size || !IsSuccess() || is_write_error)
            return;
        const unsigned char* data=(const unsigned char*)incoming;
        if (_out_stream->write(_out_stream,_cur_pos,data,data+size)){
            _cur_pos+=size;
        }else{
            is_write_error=true;
            this->close();
        }
    }
};

struct THttpRangeDownload{
    explicit THttpRangeDownload(const char* file_url):_file_url(file_url),hd(0,0),
    readedSize(0),savedSize(0),nsi(0){
        if (!doInitNetwork()){ throw std::runtime_error("InitNetwork error!"); }
        hd.SetBufsizeIn(kBestBufSize);
        hd.SetNonBlocking(false);
        hd.SetFollowRedirect(true);
        hd.SetAlwaysHandle(false);
        hd.SetUserAgent(kHttpUserAgent);
        hd.SetKeepAlive(1);
    }
    THttpDownload   hd;
    TAutoMem        cache;
    size_t          readedSize;
    size_t          savedSize;
    const TNeedSyncInfos* nsi;
    virtual ~THttpRangeDownload(){}
    
    const std::string _file_url;
    static hpatch_BOOL readSyncDataBegin(IReadSyncDataListener* listener,
                                         const TNeedSyncInfos* needSyncInfo){
        THttpRangeDownload* self=(THttpRangeDownload*)listener->readSyncDataImport;
        try{
            if (kStepMaxCacheSize>=needSyncInfo->newSyncInfo->kMatchBlockSize*2)
                self->cache.realloc(kStepMaxCacheSize);
            self->nsi=needSyncInfo;
            return hpatch_TRUE;
        }catch(...){
            return hpatch_FALSE;
        }
    }
    static void readSyncDataEnd(IReadSyncDataListener* listener){ }
    
    static hpatch_BOOL readSyncData(IReadSyncDataListener* listener,uint32_t blockIndex,
                                    hpatch_StreamPos_t posInNewSyncData,
                                    uint32_t syncDataSize,unsigned char* out_syncDataBuf){
        THttpRangeDownload* self=(THttpRangeDownload*)listener->readSyncDataImport;
        return self->readSyncData(blockIndex,posInNewSyncData,syncDataSize,out_syncDataBuf);
    }
    
    hpatch_BOOL readFromCache(uint32_t syncDataSize,unsigned char* out_syncDataBuf){
        size_t cachedSize=savedSize-readedSize;
        if (cachedSize>0){
            assert(cachedSize>=syncDataSize);
            memcpy(out_syncDataBuf,cache.data()+readedSize,syncDataSize);
            readedSize+=syncDataSize;
            return hpatch_TRUE;
        }else{
            return hpatch_FALSE;
        }
    }
    void setRanges(std::vector<TRange>& out_ranges,uint32_t blockIndex,hpatch_StreamPos_t posInNewSyncData){
        assert(readedSize==savedSize);
        readedSize=0;
        savedSize=0;
        out_ranges.clear();
        for (uint32_t i=blockIndex;i<nsi->blockCount;++i){
            hpatch_BOOL isNeedSync;
            uint32_t    syncSize;
            nsi->getBlockInfoByIndex(nsi,i,&isNeedSync,&syncSize);
            if (!isNeedSync){ posInNewSyncData+=syncSize; continue; }
            if (savedSize+syncSize>cache.size()) break; //finish
            if ((!out_ranges.empty())&&(out_ranges.back().second+1==posInNewSyncData))
                out_ranges.back().second+=syncSize;
            else if (out_ranges.size()>=kStepMaxRangCount)
                break; //finish
            else
                out_ranges.push_back(TRange(posInNewSyncData,posInNewSyncData+syncSize-1));
            savedSize+=syncSize;
            posInNewSyncData+=syncSize;
        }
    }
    
    hpatch_BOOL readSyncData(uint32_t blockIndex,hpatch_StreamPos_t posInNewSyncData,
                             uint32_t syncDataSize,unsigned char* out_syncDataBuf){
        if (readFromCache(syncDataSize,out_syncDataBuf))
            return hpatch_TRUE;
        
        hpatch_TStreamOutput out_stream;
        if (cache.empty()){//not use cache
            hd.Ranges().clear();
            hd.Ranges().push_back(TRange(posInNewSyncData,posInNewSyncData+syncDataSize-1));
            mem_as_hStreamOutput(&out_stream,out_syncDataBuf,out_syncDataBuf+syncDataSize);
        }else{
            setRanges(hd.Ranges(),blockIndex,posInNewSyncData);
            mem_as_hStreamOutput(&out_stream,cache.data(),cache.data()+savedSize);
        }
        hd._out_stream=&out_stream;
        hd._cur_pos=0;
        hd.is_finished=false;
        if (!hd.Download(_file_url))
            return hpatch_FALSE;
        
        while((hd.isOpen() || hd.HasPendingTask())&&(!hd.is_write_error)&&(!hd.is_finished)){
            if (!hd.update())
                this_thread_yield();
        }
        bool isOk=hd.IsSuccess() && hd.is_finished && (!hd.is_write_error);
        if (!isOk) return
            hpatch_FALSE;
        if (cache.empty())
            return hpatch_TRUE;
        else
            return readFromCache(syncDataSize,out_syncDataBuf);
    }
};

hpatch_BOOL download_part_by_http_open(IReadSyncDataListener* out_httpListener,const char* file_url,int threadNum){
    THttpRangeDownload* self=0;
    try {
        self=new THttpRangeDownload(file_url);
    } catch (...) {
        if (self) delete self;
        return hpatch_FALSE;
    }
    out_httpListener->readSyncDataImport=self;
    out_httpListener->readSyncDataBegin=THttpRangeDownload::readSyncDataBegin;
    out_httpListener->readSyncData=THttpRangeDownload::readSyncData;
    out_httpListener->readSyncDataEnd=THttpRangeDownload::readSyncDataEnd;
    return hpatch_TRUE;
}

hpatch_BOOL download_part_by_http_close(IReadSyncDataListener* httpListener){
    if (httpListener==0) return hpatch_TRUE;
    THttpRangeDownload* self=(THttpRangeDownload*)httpListener->readSyncDataImport;
    if (self==0) return true;
    try {
        delete self;
        return hpatch_TRUE;
    } catch (...) {
        return hpatch_FALSE;
    }
}


hpatch_BOOL download_file_by_http(const char* file_url,const hpatch_TStreamOutput* out_stream,
                                  hpatch_StreamPos_t continueDownloadPos,int threadNum){
    if (!doInitNetwork()){ throw std::runtime_error("InitNetwork error!"); }
    THttpDownload hd(out_stream,continueDownloadPos);
    hd.SetBufsizeIn(kBestBufSize);
    hd.SetNonBlocking(false);
    hd.SetFollowRedirect(true);
    hd.SetAlwaysHandle(false);
    hd.SetUserAgent(kHttpUserAgent);
    if (continueDownloadPos>0)
        hd.Ranges().push_back(TRange(continueDownloadPos,-1));
    if (!hd.Download(file_url))
        return hpatch_FALSE;
    
    while((hd.isOpen() || hd.HasPendingTask())&&(!hd.is_write_error)){
        if (!hd.update())
            this_thread_yield();
    }
    return hd.IsSuccess() && hd.is_finished && (!hd.is_write_error);
}
