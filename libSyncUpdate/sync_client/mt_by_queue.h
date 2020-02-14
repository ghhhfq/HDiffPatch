//
//  mt_by_queue.h
//  sync_client
//  Created by housisong on 2019-09-29.
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
#ifndef mt_by_queue_h
#define mt_by_queue_h
#include "../../libParallel/parallel_channel.h"
#if (_IS_USED_MULTITHREAD)
#include <vector>
namespace sync_private{

    
struct TMt_by_queue{
    const int    threadNum;
    const size_t workCount;
    inline explicit TMt_by_queue(int _threadNum,size_t _workCount,bool _isOutputNeedQueue,
                                 size_t _inputQueueCount=0)
    :threadNum(_threadNum),workCount(_workCount),finishedThreadNum(0),curWorkIndex(0),
    inputQueue(_threadNum,(_inputQueueCount>0),inputWorkIndexs,mtdataLocker),
    outputQueue(_threadNum,_isOutputNeedQueue,outputWorkIndexs,mtdataLocker){
        inputWorkIndexs.resize(threadNum,~(size_t)0);
        outputWorkIndexs.resize(threadNum,~(size_t)0);
    }
    inline void stop(){ //on a thread error
        inputQueue.stop();
        outputQueue.stop();
    }
    inline void finish(){ //for a thread work finish
        TAutoLocker _auto_locker(this);
        ++finishedThreadNum;
    }
    void waitAllFinish(){
        while(true){
            {   TAutoLocker _auto_locker(this);
                if (finishedThreadNum==threadNum) break;
            }//else wait
            this_thread_yield();
        }
    }
    struct TAutoLocker{
        inline TAutoLocker(TMt_by_queue* _mt)
            :mt(_mt){ if (mt) locker_enter(mt->mtdataLocker.locker); }
        inline ~TAutoLocker(){ if (mt) locker_leave(mt->mtdataLocker.locker); }
        TMt_by_queue* mt;
    };
    
    bool getWork(int threadIndex,size_t workIndex,size_t inputWorkIndex=~(size_t)0){
        TAutoLocker _auto_locker(this);
        if (workIndex==curWorkIndex){
            ++curWorkIndex;
            inputWorkIndexs[threadIndex]=inputWorkIndex;
            outputWorkIndexs[threadIndex]=workIndex;
            return true;
        }else{
            return false;
        }
    }

    struct TAutoInputLocker{//for get work by index
        inline TAutoInputLocker(TMt_by_queue* _mt)
            :mt(_mt){ if (mt) locker_enter(mt->readLocker.locker); }
        inline ~TAutoInputLocker(){ if (mt) locker_leave(mt->readLocker.locker); }
        TMt_by_queue* mt;
    };
private:
    int       finishedThreadNum;
    size_t    curWorkIndex;
    CHLocker  mtdataLocker;
    CHLocker  readLocker;
    std::vector<size_t>    inputWorkIndexs;
    std::vector<size_t>    outputWorkIndexs;
public://queue
    struct _TMt_a_queue{
        _TMt_a_queue(int _threadNum,bool _isQueue,
                     std::vector<size_t>& _workIndexs,CHLocker& _qlocker)
        :threadNum(_threadNum),isQueue(_isQueue),isStoped(false),curIndex(0),
        workIndexs(_workIndexs),qlocker(_qlocker){
            if (isQueue)
                lockerChannels.resize(threadNum);
        }
        void stop(){ //on a thread error
            CAutoLocker _auto_locker(this->qlocker.locker);
            if (isStoped) return;
            isStoped=true;
            if (isQueue){
                for (int i=0;i<threadNum;++i){
                    lockerChannels[i].accept(false);
                    lockerChannels[i].close();
                }
            }
        }
        bool wait(int threadIndex,size_t waitWorkIndex){
            assert(isQueue);
            {   CAutoLocker _auto_locker(this->qlocker.locker);
                if (curIndex==waitWorkIndex)
                    lockerChannels[threadIndex].send((TChanData)(1+curIndex),false);
            }
            //try wait
            CChannel& channel=lockerChannels[threadIndex];
            TChanData cData_inc=channel.accept(true);//wait
            if (cData_inc==0)
                return false; //closed
            size_t savedWorkIndex=(size_t)cData_inc-1;
            assert(savedWorkIndex==waitWorkIndex);
            return true;
        }
        void wakeup(size_t wakeupThreadByWorkIndex){
            assert(isQueue);
            CAutoLocker _auto_locker(this->qlocker.locker);
            assert(curIndex+1==wakeupThreadByWorkIndex);
            ++curIndex;
            for (int i=0;i<threadNum;++i){
                if (workIndexs[i]==curIndex)
                    lockerChannels[i].send((TChanData)(1+curIndex),false);
            }
        }
    private:
        const int       threadNum;
        const bool      isQueue;
        bool            isStoped;
        size_t          curIndex;
        struct CSChannel: public CChannel{ inline CSChannel():CChannel(1){} };
        std::vector<CSChannel> lockerChannels;
        std::vector<size_t>&   workIndexs;
        CHLocker&              qlocker;
    };
    _TMt_a_queue           inputQueue;
    _TMt_a_queue           outputQueue;
    struct TAutoQueueLocker{//for Queue by workIndex
        inline TAutoQueueLocker(_TMt_a_queue* _queue,int threadIndex,size_t _workIndex)
        :queue(_queue),workIndex(_workIndex),isWaitOk(true){
            if (queue) isWaitOk=queue->wait(threadIndex,workIndex); }
        inline ~TAutoQueueLocker(){ if ((queue!=0)&&isWaitOk) queue->wakeup(workIndex+1); }
        _TMt_a_queue* queue;
        size_t        workIndex;
        bool          isWaitOk;
    };
};

} //namespace sync_private
#endif
#endif // mt_by_queue_h
