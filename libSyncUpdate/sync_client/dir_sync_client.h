//  dir_sync_client.h
//  sync_client
//  Created by housisong on 2019-10-05.
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
#ifndef dir_sync_client_h
#define dir_sync_client_h
#include "sync_client.h"
#include "../../dirDiffPatch/dir_patch/dir_patch_types.h"
#if (_IS_NEED_DIR_DIFF_PATCH)
#include "../../dirDiffPatch/dir_diff/dir_manifest.h"
#include "../../dirDiffPatch/dir_patch/new_dir_output.h"

//sync patch oldDir to newFile
//  use get_manifest(dir) to get TManifest
int sync_patch_2file(ISyncInfoListener* listener,IReadSyncDataListener* syncDataListener,
                     const TManifest& oldManifest,const char* newSyncInfoFile,const char* outNewFile,
                     size_t kMaxOpenFileNumber,int threadNum=1);

//sync_patch can split to two steps: sync_local_diff + sync_local_patch

int sync_local_diff_2file(ISyncInfoListener* listener,IReadSyncDataListener* syncDataListener,
                          const TManifest& oldManifest,const char* newSyncInfoFile,const char* outDiffFile,
                          size_t kMaxOpenFileNumber,int threadNum=1);

int sync_local_patch_2file(ISyncInfoListener* listener,const char* inDiffFile,
                           const TManifest& oldManifest,const char* newSyncInfoFile,const char* outNewFile,
                           size_t kMaxOpenFileNumber,int threadNum=1);

struct IDirSyncPatchListener:public ISyncInfoListener{
    void*       patchImport;
    hpatch_BOOL (*patchBegin) (struct IDirSyncPatchListener* listener,
                               const TNewDataSyncInfo* newSyncInfo,TNewDirOutput* newDirOutput);
    hpatch_BOOL (*patchFinish)(struct IDirSyncPatchListener* listener,hpatch_BOOL isPatchSuccess,
                               const TNewDataSyncInfo* newSyncInfo,TNewDirOutput* newDirOutput);
};

//sync patch oldPatch to newDir
int sync_patch_2dir(IDirPatchListener* patchListener,IDirSyncPatchListener* syncListener,
                    IReadSyncDataListener* syncDataListener,
                    const TManifest& oldManifest,const char* newSyncInfoFile,const char* outNewDir,
                    size_t kMaxOpenFileNumber,int threadNum=1);
static hpatch_inline
int sync_local_diff_2dir(IDirPatchListener*,IDirSyncPatchListener* syncListener,
                         IReadSyncDataListener* syncDataListener,
                         const TManifest& oldManifest,const char* newSyncInfoFile,const char* outDiffFile,
                         size_t kMaxOpenFileNumber,int threadNum=1){
            return sync_local_diff_2file(syncListener,syncDataListener,oldManifest,newSyncInfoFile,outDiffFile,
                                         kMaxOpenFileNumber,threadNum); }
int sync_local_patch_2dir(IDirPatchListener* patchListener,IDirSyncPatchListener* syncListener,
                          const char* inDiffFile,
                          const TManifest& oldManifest,const char* newSyncInfoFile,const char* outNewDir,
                          size_t kMaxOpenFileNumber,int threadNum=1);

#endif
#endif // dir_sync_client_h
