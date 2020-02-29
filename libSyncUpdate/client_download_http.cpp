//  client_download_http.cpp
//  hsync_http: download by http(s) demo
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
#include "../file_for_patch.h"
#include "../libHDiffPatch/HDiff/private_diff/mem_buf.h"
#if (_IS_USED_MULTITHREAD)
#   include "../libParallel/parallel_import.h" //this_thread_yield
#   include "../libParallel/parallel_channel.h"
#   include "sync_client/mt_by_queue.h"
using namespace sync_private;
#endif
using namespace hdiff_private;

#ifndef _IsNeedIncludeDefaultMiniHttpHead
#   define _IsNeedIncludeDefaultMiniHttpHead 1
#endif

#if (_IsNeedIncludeDefaultMiniHttpHead)
#   include "minihttp.h" // https://github.com/sisong/minihttp
#endif
using namespace minihttp;
#if defined(_MSC_VER)
#	ifdef MINIHTTP_USE_MBEDTLS
#		pragma comment( lib, "Advapi32.lib" )
#	endif
#	if defined(_WIN32_WCE)
#		pragma comment( lib, "ws2.lib" )
#	else
#		pragma comment( lib, "ws2_32.lib" )
#	endif
#endif /* _MSC_VER */

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
const size_t kStepMaxCacheSize=kBestBufSize*4;
const size_t kStepMaxRangCount=64;
const int    kKeepAliveTime_s=5;
const char*  kHttpUserAgent="hsync" HDIFFPATCH_VERSION_STRING;

const hpatch_StreamPos_t kEmptyEndPos=~(hpatch_StreamPos_t)0;

struct THttpDownload:public HttpSocket{
    explicit THttpDownload(const hpatch_TStreamOutput* out_stream=0,
                           hpatch_StreamPos_t curOutPos=0,hpatch_StreamPos_t endOutPos=kEmptyEndPos)
    :is_finished(false),is_write_error(false),_out_stream(out_stream),
    _cur_pos(curOutPos),_end_pos(endOutPos){
        this->SetBufsizeIn(kBestBufSize);
        this->SetNonBlocking(false);
        this->SetFollowRedirect(true);
        this->SetAlwaysHandle(false);
        this->SetUserAgent(kHttpUserAgent);
    }

    inline bool isDownloadSuccess(){
        return IsSuccess() && is_finished && (!is_write_error); }
    inline bool isNeedUpdate(){
        return (isOpen() || HasPendingTask())&&(!is_write_error)&&(!is_finished);
    }
    inline void setWork(const hpatch_TStreamOutput* out_stream){
        _out_stream=out_stream;
        _cur_pos=0;
        is_finished=false;
    }
    bool doDownload(const std::string& file_url){
        if (!Download(file_url))
            return false;
        while(isNeedUpdate()){
            if (!update())
#if (_IS_USED_MULTITHREAD)
                this_thread_yield();
#else
            ;
#endif
        }
        return isDownloadSuccess();
    }
private:
    volatile bool   is_finished;
    volatile bool   is_write_error;
    
    const hpatch_TStreamOutput* _out_stream;
    hpatch_StreamPos_t          _cur_pos;
    const hpatch_StreamPos_t    _end_pos;
    virtual void _OnRequestDone(){
        is_finished = true;
    }
    virtual void _OnRecv(void *incoming, unsigned size){
        if(!size || !IsSuccess() || is_write_error)
            return;
        assert(_cur_pos+size<=_end_pos);
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
    explicit THttpRangeDownload(const char* file_url,int threadNum)
    :_file_url(file_url),cache(0),readedSize(0),nsi(0)
#if (_IS_USED_MULTITHREAD)
    ,_mt(threadNum)
#endif
    {
        if (!doInitNetwork()){ throw std::runtime_error("InitNetwork error!"); }
        assert(threadNum>=1);
#if (_IS_USED_MULTITHREAD)
        _hds.resize(threadNum,0);
        for (size_t i=0;i<_hds.size();++i){
            if (i==0)
                _hds[i]=&_hd;
            else
                _hds[i]=new THttpDownload();
            _hds[i]->SetKeepAlive(kKeepAliveTime_s);
        }
#else
        threadNum=1;
#endif
    }
    virtual ~THttpRangeDownload(){ _clearAll();  }
    static hpatch_BOOL readSyncDataBegin(IReadSyncDataListener* listener,
                                         const TNeedSyncInfos* needSyncInfo){
            THttpRangeDownload* self=(THttpRangeDownload*)listener->readSyncDataImport;
        try{
            self->readSyncDataBegin(needSyncInfo);
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
private:
    THttpDownload     _hd;
    const std::string _file_url;
    TAutoMem        _mem;
    struct TDataBuf{
        TByte*      buf;
        size_t      savedSize;
        size_t      bufSize;
        inline TDataBuf():buf(0),savedSize(0),bufSize(0){}
    };
    TDataBuf*       cache;
    size_t          readedSize;
    std::vector<TDataBuf> _dataBufs;
    const TNeedSyncInfos* nsi;
#if (_IS_USED_MULTITHREAD)
    std::vector<THttpDownload*> _hds;
    CChannel        _channelMem;
    CChannel        _channelData;
    struct _T_mt{
        THttpRangeDownload* owner;
        hpatch_StreamPos_t  posInNewSyncData;
        hpatch_StreamPos_t  newSyncDataSize;
        hpatch_BOOL         isError;
        uint32_t            blockIndex;
        size_t              bufSize;
        CHLocker            locker;
        TMt_by_queue        queue;
        inline _T_mt(int threadNum):owner(0),isError(hpatch_FALSE),queue(threadNum,true){}
    };
    _T_mt  _mt;
    friend struct _T_mt;
    inline bool isMt()const { return _hds.size()>1; }
#endif
    hpatch_BOOL readFromCache(uint32_t syncDataSize,unsigned char* out_syncDataBuf){
        if (!cache) return hpatch_FALSE;
        size_t cachedSize=cache->savedSize-readedSize;
        if (cachedSize>0){
            assert(cachedSize>=syncDataSize);
            memcpy(out_syncDataBuf,cache->buf+readedSize,syncDataSize);
            readedSize+=syncDataSize;
#if (_IS_USED_MULTITHREAD)
            if ((readedSize==cache->savedSize)&&isMt()){
                if (!_channelMem.send(cache,true)) { _mt_error(); return hpatch_FALSE; }
                cache=0;
            }
#endif
            return hpatch_TRUE;
        }else{
            return hpatch_FALSE;
        }
    }
    size_t setRanges(std::vector<TRange>& out_ranges,uint32_t& blockIndex,
                     hpatch_StreamPos_t& posInNewSyncData,size_t bufSize){
        out_ranges.clear();
        size_t sumSize=0;
        for (uint32_t i=blockIndex;i<nsi->blockCount;++i){
            hpatch_BOOL isNeedSync;
            uint32_t    syncSize;
            nsi->getBlockInfoByIndex(nsi,i,&isNeedSync,&syncSize);
            if (!isNeedSync){ posInNewSyncData+=syncSize; ++blockIndex; continue; }
            if (sumSize+syncSize>bufSize)
                break; //finish
            if ((!out_ranges.empty())&&(out_ranges.back().second+1==posInNewSyncData))
                out_ranges.back().second+=syncSize;
            else if (out_ranges.size()>=kStepMaxRangCount)
                break; //finish
            else
                out_ranges.push_back(TRange(posInNewSyncData,posInNewSyncData+syncSize-1));
            sumSize+=syncSize;
            ++blockIndex;
            posInNewSyncData+=syncSize;
        }
        return sumSize;
    }
    inline void _clearAll(){
        _closeAll();
#if (_IS_USED_MULTITHREAD)
        if (isMt()){
            for (size_t i=1;i<_hds.size();++i){
                if (_hds[i]) { delete _hds[i]; _hds[i]=0; }
            }
        }
#endif
    }
    inline void _closeAll(){
#if (_IS_USED_MULTITHREAD)
        if (isMt()){
            _mt.queue.stop();
            _channelData.close();
            _channelMem.close();
            while(0!=_channelData.accept(false)){}
            while(0!=_channelMem.accept(false)){}
            for (size_t i=1;i<_hds.size();++i){
                if (_hds[i]) _hds[i]->close();
            }
        }
#endif
        _hd.close();
    }
#if (_IS_USED_MULTITHREAD)
    void _mt_error(){
        CAutoLocker _autoLocker(_mt.locker.locker);
        if (_mt.isError) return;
        _mt.isError=hpatch_TRUE;
        _closeAll();
    }
    inline bool _mt_getData(){
        assert(cache==0);
        cache=(TDataBuf*)_channelData.accept(true);
        if (_mt.isError) return false;
        if (cache==0) { _mt_error(); return false; }
        readedSize=0;
        return true;
    }
#endif
    bool getData(THttpDownload& hd,uint32_t blockIndex,hpatch_StreamPos_t posInNewSyncData,
                 uint32_t syncDataSize,unsigned char* out_syncDataBuf){
        hpatch_TStreamOutput out_stream;
        if (!cache){
            hd.Ranges().clear();
            hd.Ranges().push_back(TRange(posInNewSyncData,posInNewSyncData+syncDataSize-1));
            mem_as_hStreamOutput(&out_stream,out_syncDataBuf,out_syncDataBuf+syncDataSize);
        }else{
            assert(readedSize==cache->savedSize);
            cache->savedSize=setRanges(hd.Ranges(),blockIndex,
                                       posInNewSyncData,cache->bufSize);
            readedSize=0;
            mem_as_hStreamOutput(&out_stream,cache->buf,cache->buf+cache->savedSize);
        }
        hd.setWork(&out_stream);
        return hd.doDownload(_file_url);
    }

    inline hpatch_BOOL readSyncData(uint32_t blockIndex,hpatch_StreamPos_t posInNewSyncData,
                                    uint32_t syncDataSize,unsigned char* out_syncDataBuf){
        if (readFromCache(syncDataSize,out_syncDataBuf))
            return hpatch_TRUE;
#if (_IS_USED_MULTITHREAD)
        if (isMt()){
            if (!_mt_getData()) return hpatch_FALSE;
        }else
#endif
            if (!getData(_hd,blockIndex,posInNewSyncData,syncDataSize,out_syncDataBuf)) return hpatch_FALSE;
        if (!cache)
            return hpatch_TRUE;
        else
            return readFromCache(syncDataSize,out_syncDataBuf);
    }
    
#if (_IS_USED_MULTITHREAD)
    static void _mt_thread_download(int threadIndex,void* workData){
        _T_mt* _mt=(_T_mt*)workData;
        THttpRangeDownload* self=(THttpRangeDownload*)_mt->owner;
        THttpDownload& hd=*self->_hds[threadIndex];
        while(true){
            size_t savedSize=0;
            size_t workIndex=-1;
            { //get work range
                CAutoLocker _autoLocker(_mt->locker.locker);
                if (_mt->isError) break;
                savedSize=self->setRanges(hd.Ranges(),_mt->blockIndex,
                                          _mt->posInNewSyncData,_mt->bufSize);
                assert(_mt->posInNewSyncData<=_mt->newSyncDataSize);
                if (savedSize==0)
                    break; //finish
                workIndex=_mt->queue.getCurWorkIndex();
                if (!_mt->queue.getWork(threadIndex,workIndex))
                    assert(false);
                
            }
            TDataBuf* buf=0;
            { //get buf
                buf=(TDataBuf*)self->_channelMem.accept(true);
                if (buf==0){
                    self->_mt_error(); break; } //error
                assert(savedSize<=buf->bufSize);
                buf->savedSize=savedSize;
            }
            { //download
                hpatch_TStreamOutput out_stream;
                mem_as_hStreamOutput(&out_stream,buf->buf,buf->buf+savedSize);
                hd.setWork(&out_stream);
                if (!hd.doDownload(self->_file_url)){
                    self->_mt_error(); break; } //error
            }
            { //out data order by workIndex
                TMt_by_queue::TAutoQueueLocker _autoLocker(&_mt->queue.outputQueue,threadIndex,workIndex);
                if (!self->_channelData.send(buf,true)){
                    self->_mt_error(); break; } //error
            }
        }
        hd.close();
    }
#endif

    void readSyncDataBegin(const TNeedSyncInfos* needSyncInfo){
        nsi=needSyncInfo;
        assert(cache==0);
        readedSize=0;
        size_t bufSize=0;
#if (_IS_USED_MULTITHREAD)
        if (isMt()){//must use caches
            hpatch_StreamPos_t threadDataSize=( (hpatch_StreamPos_t)needSyncInfo->blockCount
                                              * needSyncInfo->kMatchBlockSize ) / _hds.size();
            if (threadDataSize>=kStepMaxCacheSize)
                bufSize=kStepMaxCacheSize;
            else
                bufSize=(size_t)threadDataSize;
            if (bufSize<needSyncInfo->kMatchBlockSize)
                bufSize=needSyncInfo->kMatchBlockSize;
        }else
#endif
            if ((kStepMaxCacheSize>=needSyncInfo->kMatchBlockSize*2))
                bufSize=kStepMaxCacheSize;
            //else not need cache
        if (bufSize>0){
#if (_IS_USED_MULTITHREAD)
            if (isMt()){
                _dataBufs.resize(_hds.size()+1);
                _mem.realloc(bufSize*_dataBufs.size());
                for( size_t i=0;i<_dataBufs.size();++i){
                    _dataBufs[i].buf=_mem.data()+i*bufSize;
                    _dataBufs[i].bufSize=bufSize;
                    _channelMem.send(_dataBufs.data()+i,true);
                }
                cache=0;
            }else
#endif
            {
                _dataBufs.resize(1);
                _mem.realloc(bufSize);
                _dataBufs[0].buf=_mem.data();
                _dataBufs[0].bufSize=bufSize;
                cache=_dataBufs.data();
                cache->savedSize=0;
            }
        }
#if (_IS_USED_MULTITHREAD)
        if (isMt()){
            _mt.owner=this;
            _mt.isError=hpatch_FALSE;
            _mt.blockIndex=0;
            _mt.posInNewSyncData=0;
            _mt.bufSize=bufSize;
            _mt.newSyncDataSize=nsi->newSyncDataSize;
            thread_parallel((int)_hds.size(),_mt_thread_download,&_mt,hpatch_FALSE,0);
        }
#endif
    }
};

hpatch_BOOL download_part_by_http_open(IReadSyncDataListener* out_httpListener,const char* file_url,int threadNum){
    THttpRangeDownload* self=0;
    try {
        self=new THttpRangeDownload(file_url,threadNum);
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

static hpatch_BOOL _download(const char* file_url,const hpatch_TStreamOutput* out_stream,
                             hpatch_StreamPos_t continueDownloadPos,hpatch_StreamPos_t& endPos){
    if (!doInitNetwork()){ throw std::runtime_error("InitNetwork error!"); }
    assert(continueDownloadPos<=endPos);
    THttpDownload hd(out_stream,continueDownloadPos);
    if ((continueDownloadPos>0)||(endPos!=kEmptyEndPos)){
        hpatch_StreamPos_t rangEnd=(endPos==kEmptyEndPos)?kEmptyEndPos:(endPos-1);
        hd.Ranges().push_back(TRange(continueDownloadPos,rangEnd));
    }
    if (!hd.doDownload(file_url))
        return hpatch_FALSE;
    if (hd.Ranges().empty())
        endPos=continueDownloadPos+hd.GetContentLen();
    else
        endPos=hd.GetRangsBytesLen();
    return hpatch_TRUE;
}

#if (_IS_USED_MULTITHREAD)
    struct TNeedSyncInfosImport:public TNeedSyncInfos{
        uint32_t  outBlockIndex;
    };
    static void _getBlockInfoByIndex(const TNeedSyncInfos* needSyncInfos,uint32_t blockIndex,
                                     hpatch_BOOL* out_isNeedSync,uint32_t* out_syncSize){
        const TNeedSyncInfosImport* self=(const TNeedSyncInfosImport*)needSyncInfos->import;
        assert(blockIndex<self->blockCount);
        *out_isNeedSync=(blockIndex>=self->outBlockIndex)?hpatch_TRUE:hpatch_FALSE;
        if (blockIndex+1<self->blockCount){
            *out_syncSize=self->kMatchBlockSize;
        }else{
            *out_syncSize=(uint32_t)(self->newSyncDataSize-self->kMatchBlockSize*(self->blockCount-1));
        }
    }

static hpatch_BOOL _mt_download(const char* file_url,const hpatch_TStreamOutput* out_stream,
                                hpatch_StreamPos_t continueDownloadPos,hpatch_StreamPos_t endPos,int threadNum){
    uint32_t blockSize=kBestBufSize/2;
    hpatch_StreamPos_t _blockCount;
    do{
        blockSize*=2;
        _blockCount=getSyncBlockCount(endPos,blockSize);
    }while(_blockCount!=(uint32_t)_blockCount);
    
    hpatch_StreamPos_t outPos=toAlignRangeSize(continueDownloadPos,blockSize);
    if (outPos>continueDownloadPos){
        hpatch_StreamPos_t _tempEndPos=outPos;
        if (!_download(file_url,out_stream,continueDownloadPos,_tempEndPos)) return hpatch_FALSE;
        assert(_tempEndPos==endPos);
    }
    uint32_t outBlockIndex=(uint32_t)(outPos/blockSize);
    
    unsigned char* _buf=(unsigned char*)malloc(blockSize);
    if (!_buf)  return hpatch_FALSE;
    IReadSyncDataListener listener; memset(&listener,0,sizeof(listener));
    if (!download_part_by_http_open(&listener,file_url,threadNum)) return hpatch_FALSE;
    TNeedSyncInfosImport nsi; memset(&nsi,0,sizeof(nsi));
    if (listener.readSyncDataBegin){
        nsi.import=&nsi;
        nsi.newDataSize=endPos;
        nsi.newSyncDataSize=endPos;
        nsi.newSyncInfoSize=0; //no
        nsi.kMatchBlockSize=blockSize;
        nsi.blockCount=(uint32_t)_blockCount;
        nsi.needSyncBlockCount=nsi.blockCount-outBlockIndex;
        nsi.needSyncSumSize=endPos-outPos;
        nsi.outBlockIndex=outBlockIndex;
        nsi.getBlockInfoByIndex=_getBlockInfoByIndex;
        if (!listener.readSyncDataBegin(&listener,&nsi)) return hpatch_FALSE;
    }
    hpatch_BOOL result=hpatch_TRUE;
    for (uint32_t i=outBlockIndex;i<(uint32_t)_blockCount; ++i) {
        uint32_t readSize=blockSize;
        if (readSize+outPos>endPos)
            readSize=(uint32_t)(endPos-outPos);
        if (!listener.readSyncData(&listener,i,outPos,readSize,_buf)){
            result=hpatch_FALSE; break; }
        if (!out_stream->write(out_stream,outPos,_buf,_buf+readSize)){
            result=hpatch_FALSE; break; }
        outPos+=readSize;
    }
    free(_buf);
    if (result)
        assert(outPos==endPos);
    if (listener.readSyncDataEnd)
        listener.readSyncDataEnd(&listener);
    if (!download_part_by_http_close(&listener))
        result=hpatch_FALSE;
    return result;
}
#endif

hpatch_BOOL download_file_by_http(const char* file_url,const hpatch_TStreamOutput* out_stream,
                                  hpatch_StreamPos_t continueDownloadPos,int threadNum){
    bool isNeedGetEndPos=(continueDownloadPos>0);
#if (_IS_USED_MULTITHREAD)
    if (threadNum>1)
        isNeedGetEndPos=true;
#endif
    hpatch_StreamPos_t endPos=kEmptyEndPos;
    if (isNeedGetEndPos){
        hpatch_TStreamOutput temp_stream; unsigned char tempBuf[1];
        mem_as_hStreamOutput(&temp_stream,tempBuf,tempBuf+1);
        endPos=1;
        if (!_download(file_url,&temp_stream,0,endPos)) return hpatch_FALSE;//not request "HEAD" to get contentLen
        if (continueDownloadPos>endPos) return hpatch_FALSE;
        if (continueDownloadPos==endPos) return hpatch_TRUE;
    }
#if (_IS_USED_MULTITHREAD)
    if ((threadNum>1)&&(endPos-continueDownloadPos>=kStepMaxCacheSize)){
        assert(endPos!=kEmptyEndPos);
        return _mt_download(file_url,out_stream,continueDownloadPos,endPos,threadNum);
    }else
#endif
    {
        endPos=kEmptyEndPos;
        return _download(file_url,out_stream,continueDownloadPos,endPos);
    }
}
