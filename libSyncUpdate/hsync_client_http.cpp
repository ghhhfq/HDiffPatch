//  hsync_client_http.cpp
//  hsync_client_http: client for sync patch by http(s)
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
#define  _IS_NEED_DOWNLOAD_EMULATION 0
#include "hsync_client_demo.cpp"
#include "client_download_http/client_download_http.h"

bool openNewSyncDataByUrl(ISyncPatchListener* listener,const char* newSyncDataFile_url){
    return download_part_by_http_open(&listener->readSyncDataListener,newSyncDataFile_url);
}
bool closeNewSyncData(ISyncPatchListener* listener){
    return download_part_by_http_close(&listener->readSyncDataListener);
}
bool downloadNewSyncInfoFromUrl(const char* newSyncInfoFile_url,const hpatch_TStreamOutput* out_stream){
    return download_file_by_http(newSyncInfoFile_url,out_stream);
}
