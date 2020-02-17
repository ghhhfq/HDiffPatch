//  sync_client_type.h
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
#ifndef sync_client_type_h
#define sync_client_type_h
#include "../../libHDiffPatch/HPatch/patch_types.h"
#include "../../libHDiffPatch/HPatch/checksum_plugin.h"
#include "../../dirDiffPatch/dir_patch/dir_patch_types.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

hpatch_inline static
hpatch_StreamPos_t getSyncBlockCount(hpatch_StreamPos_t newDataSize,uint32_t kMatchBlockSize){
    return (newDataSize+(kMatchBlockSize-1))/kMatchBlockSize; }


typedef struct TSameNewBlockPair{
    uint32_t  curIndex;
    uint32_t  sameIndex; // sameIndex < curIndex;
} TSameNewBlockPair;

typedef struct TNewDataSyncInfo{
    const unsigned char*    externData_begin;//externData is used by user
    const unsigned char*    externData_end;
    const char*             compressType;
    const char*             strongChecksumType;
    uint32_t                kStrongChecksumByteSize;
    uint32_t                kMatchBlockSize;
    uint32_t                samePairCount;
    uint8_t                 isDirSyncInfo;
    uint8_t                 is32Bit_rollHash;
    hpatch_StreamPos_t      newDataSize;      // newData version size;
    hpatch_StreamPos_t      newSyncDataSize;  // .hsynd size ,saved newData or complessed newData
    hpatch_StreamPos_t      newSyncInfoSize;  // .hsyni size ,saved newData's info
    unsigned char*          newDataCheckChecksum; // out new data's strongChecksum's checksum
    unsigned char*          infoFullChecksum; // this info data's strongChecksum
    TSameNewBlockPair*      samePairList;
    uint32_t*               savedSizes;
    void*                   rollHashs;
    unsigned char*          partChecksums;
#if (_IS_NEED_DIR_DIFF_PATCH)
    size_t                  dir_newPathCount;
    uint8_t                 dir_newNameList_isCString;
    const void*             dir_utf8NewNameList;//is const char** or const std::string* type
    const char*             dir_utf8NewRootPath;
    hpatch_StreamPos_t*     dir_newSizeList;
    size_t                  dir_newExecuteCount;
    size_t*                 dir_newExecuteIndexList;
#endif
    hpatch_TChecksum*       _strongChecksumPlugin;
    hpatch_TDecompress*     _decompressPlugin;
    void*                   _import;
} TNewDataSyncInfo;

struct TNeedSyncInfos;
typedef void (*TSync_getBlockInfoByIndex)(const struct TNeedSyncInfos* needSyncInfos,uint32_t blockIndex,
                                          hpatch_BOOL* out_isNeedSync,uint32_t* out_syncSize);
typedef struct TNeedSyncInfos{
    hpatch_StreamPos_t          newDataSize;     // new data size
    hpatch_StreamPos_t          newSyncDataSize; // new data size or .hsynd file size
    hpatch_StreamPos_t          newSyncInfoSize; // .hsyni file size
    uint32_t                    kMatchBlockSize;
    uint32_t                    blockCount;
    uint32_t                    needSyncBlockCount;
    hpatch_StreamPos_t          needSyncSumSize; // all need download from new data or .hsynd file
    TSync_getBlockInfoByIndex   getBlockInfoByIndex;
    void*                       import; //private
} TNeedSyncInfos;

typedef struct IReadSyncDataListener{
    void*       readSyncDataImport;
    //readSyncDataBegin can null
    hpatch_BOOL (*readSyncDataBegin)(IReadSyncDataListener* listener,const TNeedSyncInfos* needSyncInfo);
    //download range data
    hpatch_BOOL (*readSyncData)     (IReadSyncDataListener* listener,uint32_t blockIndex,
                                     hpatch_StreamPos_t posInNewSyncData,
                                     uint32_t syncDataSize,unsigned char* out_syncDataBuf);
    //readSyncDataEnd can null
    void        (*readSyncDataEnd)  (IReadSyncDataListener* listener);
    
    //localPatch_openOldPoss  private default set null
    void   (*localPatch_openOldPoss)(IReadSyncDataListener* listener,void* localPosHandle);
} IReadSyncDataListener;

typedef struct TSyncDownloadPlugin{
    //download part of file
    hpatch_BOOL (*download_part_open) (IReadSyncDataListener* out_listener,const char* file_url,int threadNum);
    hpatch_BOOL (*download_part_close)(IReadSyncDataListener* listener);
    //download file
    hpatch_BOOL (*download_file)      (const char* file_url,const hpatch_TStreamOutput* out_stream,
                                       hpatch_StreamPos_t continueDownloadPos,int threadNum);
} TSyncDownloadPlugin;

#ifdef __cplusplus
}
#endif
#endif //sync_client_type_h
