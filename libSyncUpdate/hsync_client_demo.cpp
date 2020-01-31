//  hsync_client.cpp
//  hsync_client: client for sync patch
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
#include <vector>
#include "../_clock_for_demo.h"
#include "../_atosize.h"
#include "../libParallel/parallel_import.h"
#include "../file_for_patch.h"
#include "../_dir_ignore.h"
#include "../libHDiffPatch/HDiff/private_diff/mem_buf.h"

#include "sync_client/sync_client.h"
#if (_IS_NEED_DIR_DIFF_PATCH)
#   include "sync_client/dir_sync_client.h"
#   define _IS_NEED_tempDirPatchListener 0
#   include "../hpatch_dir_listener.h"
#endif
#ifndef _IS_NEED_MAIN
#   define  _IS_NEED_MAIN 1
#endif

#ifndef _IS_NEED_DOWNLOAD_EMULATION
#   define  _IS_NEED_DOWNLOAD_EMULATION 1
#endif
#if (_IS_NEED_DOWNLOAD_EMULATION)
//simple file demo
#   include "client_download_demo.h"
static inline bool openNewSyncDataByUrl(ISyncPatchListener* listener,const char* newSyncDataFile){
    return downloadEmulation_open_by_file(&listener->readSyncDataListener,newSyncDataFile);
}
static inline bool closeNewSyncData(ISyncPatchListener* listener){
    return downloadEmulation_close(&listener->readSyncDataListener);
}
#else //download by http/https ?
bool openNewSyncDataByUrl(ISyncPatchListener* listener,const char* newSyncDataFile_url);
bool closeNewSyncData(ISyncPatchListener* listener);
bool downloadNewSyncInfoFromUrl(const char* newSyncInfoFile_url,const hpatch_TStreamOutput* out_stream);
#endif

#ifndef _IS_NEED_DEFAULT_CompressPlugin
#   define _IS_NEED_DEFAULT_CompressPlugin 1
#endif
#if (_IS_NEED_DEFAULT_CompressPlugin)
//===== select needs decompress plugins or change to your plugin=====
#   define _CompressPlugin_zlib
#   define _CompressPlugin_lzma
//#   define _CompressPlugin_bz2
#endif

#include "../decompress_plugin_demo.h"

#ifndef _IS_NEED_DEFAULT_ChecksumPlugin
#   define _IS_NEED_DEFAULT_ChecksumPlugin 1
#endif
#if (_IS_NEED_DEFAULT_ChecksumPlugin)
//===== select needs checksum plugins or change to your plugin=====
#   define _ChecksumPlugin_md5
#endif

#include "../checksum_plugin_demo.h"

static void printUsage(){
#if (_IS_NEED_DOWNLOAD_EMULATION)
    printf("test sync patch: [options] oldPath newSyncInfoFile newSyncDataFile_test outNewPath\n"
#else
    printf("http sync patch: [options] oldPath newSyncInfoFile newSyncDataFile_url outNewPath\n"
           "http download: -U#newSyncInfoFile_url newSyncInfoFile\n"
#endif
#if (_IS_NEED_DIR_DIFF_PATCH)
           "  oldPath can be file or directory(folder),\n"
#endif
           "  if oldPath is empty input parameter \"\" )\n"
           "options:\n"
#if (_IS_NEED_DOWNLOAD_EMULATION)
#else
           "  -U#newSyncInfoFile_url\n"
           "    http download newSyncInfoFile from newSyncInfoFile_url befor sync patch;\n"
#endif
#if (_IS_USED_MULTITHREAD)
           "  -p-parallelThreadNumber\n"
           "    if parallelThreadNumber>1 then open multi-thread Parallel mode;\n"
           "    DEFAULT -p-4; requires more memory!\n"
#endif
           "  -C-checksumSets\n"
           "      set Checksum data for patch, DEFAULT -C-sync;\n"
           "      checksumSets support:\n"
           "        -C-info         checksum newSyncInfoFile;\n"
           "        -C-sync         checksum outNewPath's data sync from newSyncDataFile;\n"
           "        -C-no           no checksum;\n"
           "        -C-all          same as: -C-info-sync;\n"
#if (_IS_NEED_DIR_DIFF_PATCH)
           "  -n-maxOpenFileNumber\n"
           "      limit Number of open files at same time when oldPath is directory;\n"
           "      maxOpenFileNumber>=8, DEFAULT -n-24, the best limit value by different\n"
           "        operating system.\n"
           "  -g#ignorePath[#ignorePath#...]\n"
           "      set iGnore path list in newDataPath directory; ignore path list such as:\n"
           "        #.DS_Store#desktop.ini#*thumbs*.db#.git*#.svn/#cache_*/00*11/*.tmp\n"
           "      # means separator between names; (if char # in name, need write #: )\n"
           "      * means can match any chars in name; (if char * in name, need write *: );\n"
           "      / at the end of name means must match directory;\n"
#endif
           "  -f  Force overwrite, ignore write path already exists;\n"
           "      DEFAULT (no -f) not overwrite and then return error;\n"
           "      support oldPath outNewPath same path!(patch to tempPath and overwrite old)\n"
           "      if used -f and outNewPath is exist file:\n"
#if (_IS_NEED_DIR_DIFF_PATCH)
           "        if patch output file, will overwrite;\n"
#else
           "        will overwrite;\n"
#endif
#if (_IS_NEED_DIR_DIFF_PATCH)
           "        if patch output directory, will always return error;\n"
#endif
           "      if used -f and outNewPath is exist directory:\n"
#if (_IS_NEED_DIR_DIFF_PATCH)
           "        if patch output file, will always return error;\n"
#else
           "        will always return error;\n"
#endif
#if (_IS_NEED_DIR_DIFF_PATCH)
           "        if patch output directory, will overwrite, but not delete\n"
           "          needless existing files in directory.\n"
#endif
           "  -h or -?\n"
           "      output Help info (this usage).\n"
           "  -v  output Version info.\n\n"
           );
}

int sync_client_cmd_line(int argc, const char * argv[]);

int sync_patch_2file(const char* outNewFile,const char* oldPath,bool isSamePath,bool oldIsDir,
                     const std::vector<std::string>& ignoreOldPathList,
                     const char* newSyncInfoFile,const char* newSyncDataFile_url,
                     TSyncPatchChecksumSet checksumSet,size_t kMaxOpenFileNumber,size_t threadNum);
#if (_IS_NEED_DIR_DIFF_PATCH)
int  sync_patch_2dir(const char* outNewDir,const char* oldPath,bool isSamePath,bool oldIsDir,
                     const std::vector<std::string>& ignoreOldPathList,
                     const char* newSyncInfoFile,const char* newSyncDataFile_url,
                     TSyncPatchChecksumSet checksumSet,size_t kMaxOpenFileNumber,size_t threadNum);
#endif
           
#if (_IS_NEED_DOWNLOAD_EMULATION)
#else
int downloadNewSyncInfoFile(const char* newSyncInfoFile_url,const char* outNewSyncInfoFile);
#endif


#if (_IS_NEED_MAIN)
#   if (_IS_USED_WIN32_UTF8_WAPI)
int wmain(int argc,wchar_t* argv_w[]){
    hdiff_private::TAutoMem  _mem(hpatch_kPathMaxSize*4);
    char** argv_utf8=(char**)_mem.data();
    if (!_wFileNames_to_utf8((const wchar_t**)argv_w,argc,argv_utf8,_mem.size()))
        return kSyncClient_optionsError;
    SetDefaultStringLocale();
    return sync_client_cmd_line(argc,(const char**)argv_utf8);
}
#   else
int main(int argc,char* argv[]){
    return  sync_client_cmd_line(argc,(const char**)argv);
}
#   endif
#endif


//ISyncInfoListener::findDecompressPlugin
static hpatch_TDecompress* _findDecompressPlugin(ISyncInfoListener* listener,const char* compressType){
    if (compressType==0) return 0; //ok
    hpatch_TDecompress* decompressPlugin=0;
#ifdef  _CompressPlugin_zlib
    if ((!decompressPlugin)&&zlibDecompressPlugin.is_can_open(compressType))
        decompressPlugin=&zlibDecompressPlugin;
#endif
#ifdef  _CompressPlugin_bz2
    if ((!decompressPlugin)&&bz2DecompressPlugin.is_can_open(compressType))
        decompressPlugin=&bz2DecompressPlugin;
#endif
#ifdef  _CompressPlugin_lzma
    if ((!decompressPlugin)&&lzmaDecompressPlugin.is_can_open(compressType))
        decompressPlugin=&lzmaDecompressPlugin;
#endif
    if (decompressPlugin==0){
        printf("  sync_patch can't decompress type: \"%s\"\n",compressType);
        return 0; //unsupport error
    }else{
        printf("  sync_patch run with decompress plugin: \"%s\"\n",compressType);
        return decompressPlugin; //ok
    }
}
//ISyncInfoListener::findChecksumPlugin
static hpatch_TChecksum* _findChecksumPlugin(ISyncInfoListener* listener,const char* strongChecksumType){
    if (strongChecksumType==0) return 0; //ok
    hpatch_TChecksum* strongChecksumPlugin=0;
#ifdef  _ChecksumPlugin_md5
    if ((!strongChecksumPlugin)&&(0==strcmp(strongChecksumType,md5ChecksumPlugin.checksumType())))
        strongChecksumPlugin=&md5ChecksumPlugin;
#endif
    if (strongChecksumPlugin==0){
        printf("  sync_patch can't found checksum type: \"%s\"\n",strongChecksumType);
        return 0; //unsupport error
    }else{
        printf("  sync_patch run with strongChecksum plugin: \"%s\"\n",strongChecksumType);
        return strongChecksumPlugin; //ok
    }
}

    static void printMatchResult(const TNeedSyncInfos* nsi) {
        const TNewDataSyncInfo* newi=nsi->newSyncInfo;
        const uint32_t kBlockCount=nsi->blockCount;
        printf("  syncBlockCount: %d, /%d=%.1f%%\n  syncDataSize: %" PRIu64 "\n",
               nsi->needSyncBlockCount,kBlockCount,100.0*nsi->needSyncBlockCount/kBlockCount,nsi->needSyncSumSize);
        hpatch_StreamPos_t downloadSize=newi->newSyncInfoSize+nsi->needSyncSumSize;
        printf("  downloadSize: %" PRIu64 "+%" PRIu64 "= %" PRIu64 ", /%" PRIu64 "=%.1f%%",
               newi->newSyncInfoSize,nsi->needSyncSumSize,downloadSize,
               newi->newDataSize,100.0*downloadSize/newi->newDataSize);
        if (newi->newSyncDataSize<newi->newDataSize){
            hpatch_StreamPos_t maxDownloadSize=newi->newSyncInfoSize+newi->newSyncDataSize;
            printf(" (/%" PRIu64 "=%.1f%%)",maxDownloadSize,100.0*downloadSize/maxDownloadSize);
        }
        printf("\n");
    }
//ISyncInfoListener::needSyncInfo
static void _needSyncInfo(ISyncInfoListener* listener,const TNeedSyncInfos* needSyncInfo){
    printMatchResult(needSyncInfo);
}


static hpatch_BOOL _toChecksumSet(const char* psets,TSyncPatchChecksumSet* out_checksumSet){
    while (hpatch_TRUE) {
        const char* pend=findUntilEnd(psets,'-');
        size_t len=(size_t)(pend-psets);
        if (len==0) return hpatch_FALSE; //error no set
        if        ((len==4)&&(0==memcmp(psets,"info",len))){
            out_checksumSet->isChecksumNewSyncInfo=true;
        }else  if ((len==4)&&(0==memcmp(psets,"sync",len))){
            out_checksumSet->isChecksumNewSyncData=true;
        }else  if ((len==3)&&(0==memcmp(psets,"all",len))){
            out_checksumSet->isChecksumNewSyncInfo=true;
            out_checksumSet->isChecksumNewSyncData=true;
        }else  if ((len==2)&&(0==memcmp(psets,"no",len))){
            out_checksumSet->isChecksumNewSyncInfo=false;
            out_checksumSet->isChecksumNewSyncData=false;
        }else{
            return hpatch_FALSE;//error unknow set
        }
        if (*pend=='\0')
            return hpatch_TRUE; //ok
        else
            psets=pend+1;
    }
}

#define _options_check(value,errorInfo) do{ \
    if (!(value)) { fprintf(stderr,"options " errorInfo " ERROR!\n\n"); \
                    printUsage(); return kSyncClient_optionsError; } }while(0)
#define _printErr(fmt,errorInfo) do{ fprintf(stderr,fmt " ERROR!\n",errorInfo); }while(0)
#define _return_check(value,exitCode,fmt,errorInfo) do{ \
    if (!(value)) { _printErr(fmt,errorInfo); return exitCode; } }while(0)

#define _kNULL_VALUE    (-1)
#define _kNULL_SIZE     (~(size_t)0)

#define _THREAD_NUMBER_NULL     0
#define _THREAD_NUMBER_MIN      1
#define _THREAD_NUMBER_DEFUALT  4
#define _THREAD_NUMBER_MAX      (1<<8)

int sync_client_cmd_line(int argc, const char * argv[]) {
    size_t      threadNum = _THREAD_NUMBER_NULL;
    hpatch_BOOL isForceOverwrite=_kNULL_VALUE;
    hpatch_BOOL isOutputHelp=_kNULL_VALUE;
    hpatch_BOOL isOutputVersion=_kNULL_VALUE;
    hpatch_BOOL isOldPathInputEmpty=_kNULL_VALUE;
    TSyncPatchChecksumSet checksumSet={false,true};//checksumSet DEFAULT
    const char* newSyncInfoFile_url=0;
//_IS_NEED_DIR_DIFF_PATCH
    size_t                      kMaxOpenFileNumber=_kNULL_SIZE; //only used in oldPath is dir
    std::vector<std::string>    ignoreOldPathList;
//
    std::vector<const char *> arg_values;
    for (int i=1; i<argc; ++i) {
        const char* op=argv[i];
        _options_check(op!=0,"?");
        if (op[0]!='-'){
            hpatch_BOOL isEmpty=(strlen(op)==0);
            if (isEmpty){
                if (isOldPathInputEmpty==_kNULL_VALUE)
                    isOldPathInputEmpty=hpatch_TRUE;
                else
                    _options_check(!isEmpty,"?"); //error return
            }else{
                if (isOldPathInputEmpty==_kNULL_VALUE)
                    isOldPathInputEmpty=hpatch_FALSE;
            }
            arg_values.push_back(op); //file path
            continue;
        }
        switch (op[1]) {
#if (_IS_USED_MULTITHREAD)
            case 'p':{
                _options_check((threadNum==_THREAD_NUMBER_NULL)&&(op[2]=='-'),"-p-?");
                const char* pnum=op+3;
                _options_check(a_to_size(pnum,strlen(pnum),&threadNum),"-p-?");
                _options_check(threadNum>=_THREAD_NUMBER_MIN,"-p-?");
            } break;
#endif
#if (_IS_NEED_DOWNLOAD_EMULATION)
#else
            case 'U':{
                const char* purl=op+3;
                _options_check((newSyncInfoFile_url==0)&&(op[2]=='#')&&(strlen(purl)>0),"-U#?");
                newSyncInfoFile_url=purl;
            } break;
#endif
            case 'C':{
                const char* psets=op+3;
                _options_check((op[2]=='-'),"-C-?");
                checksumSet.isChecksumNewSyncInfo=false;
                checksumSet.isChecksumNewSyncData=false;//all set false
                _options_check(_toChecksumSet(psets,&checksumSet),"-C-?");
            } break;
#if (_IS_NEED_DIR_DIFF_PATCH)
            case 'n':{
                const char* pnum=op+3;
                _options_check((kMaxOpenFileNumber==_kNULL_SIZE)&&(op[2]=='-'),"-n-?");
                _options_check(kmg_to_size(pnum,strlen(pnum),&kMaxOpenFileNumber),"-n-?");
            } break;
            case 'g':{
                if (op[2]=='#'){ //-g#
                    const char* plist=op+3;
                    _options_check(_getIgnorePathSetList(ignoreOldPathList,plist),"-g#?");
                }else{
                    _options_check(hpatch_FALSE,"-g?");
                }
            } break;
#endif
            case 'f':{
                _options_check((isForceOverwrite==_kNULL_VALUE)&&(op[2]=='\0'),"-f");
                isForceOverwrite=hpatch_TRUE;
            } break;
            case '?':
            case 'h':{
                _options_check((isOutputHelp==_kNULL_VALUE)&&(op[2]=='\0'),"-h");
                isOutputHelp=hpatch_TRUE;
            } break;
            case 'v':{
                _options_check((isOutputVersion==_kNULL_VALUE)&&(op[2]=='\0'),"-v");
                isOutputVersion=hpatch_TRUE;
            } break;
            default: {
                _options_check(hpatch_FALSE,"?");
            } break;
        }//swich
    }
    
    if (isOutputHelp==_kNULL_VALUE)
        isOutputHelp=hpatch_FALSE;
    if (isOutputVersion==_kNULL_VALUE)
        isOutputVersion=hpatch_FALSE;
    if (isForceOverwrite==_kNULL_VALUE)
        isForceOverwrite=hpatch_FALSE;
#if (_IS_USED_MULTITHREAD)
    if (threadNum==_THREAD_NUMBER_NULL)
        threadNum=_THREAD_NUMBER_DEFUALT;
    else if (threadNum>_THREAD_NUMBER_MAX)
        threadNum=_THREAD_NUMBER_MAX;
#else
    threadNum=1;
#endif
#if (_IS_NEED_DIR_DIFF_PATCH)
    if (kMaxOpenFileNumber==_kNULL_SIZE)
        kMaxOpenFileNumber=kMaxOpenFileNumber_default_patch;
    if (kMaxOpenFileNumber<kMaxOpenFileNumber_default_min)
        kMaxOpenFileNumber=kMaxOpenFileNumber_default_min;
#endif
    if (isOldPathInputEmpty==_kNULL_VALUE)
        isOldPathInputEmpty=hpatch_FALSE;
    
    if (isOutputHelp||isOutputVersion){
#if (_IS_NEED_DOWNLOAD_EMULATION)
        printf("HDiffPatch::hsync_client_demo v" HDIFFPATCH_VERSION_STRING "\n\n");
#else
        printf("HDiffPatch::hsync_client_http v" HDIFFPATCH_VERSION_STRING "\n\n");
#endif
        if (isOutputHelp)
            printUsage();
        if (arg_values.empty())
            return kSyncClient_ok; //ok
    }

    const bool isOnlyDownload=(newSyncInfoFile_url!=0) && (arg_values.size()==1);
    _options_check((arg_values.size()==4)||isOnlyDownload,"input count");
    const char* oldPath             =isOnlyDownload?            0:arg_values[0];
    const char* newSyncInfoFile     =isOnlyDownload?arg_values[0]:arg_values[1]; // .hsyni
    const char* newSyncDataFile_url =isOnlyDownload?            0:arg_values[2]; // .hsynd
    const char* outNewPath          =isOnlyDownload?            0:arg_values[3];
    double time0=clock_s();
#if (_IS_NEED_DOWNLOAD_EMULATION)
#else
    if (newSyncInfoFile_url){
        if (!isForceOverwrite)
            _return_check(_isPathNotExist(newSyncInfoFile),kSyncClient_overwriteNewSyncInfoFileError,
                          "%s already exists, overwrite",newSyncInfoFile);
        int result=downloadNewSyncInfoFile(newSyncInfoFile_url,newSyncInfoFile);
        _return_check(result==kSyncClient_ok,result,"download from url:%s",newSyncInfoFile_url);
        if (isOnlyDownload)
            return result;
        //else continue sync patch
    }
#endif
    
    const bool isSamePath=hpatch_getIsSamePath(oldPath,outNewPath)!=0;
    hpatch_TPathType   outNewPathType;
    _return_check(hpatch_getPathStat(outNewPath,&outNewPathType,0),
                  kSyncClient_newPathTypeError,"get %s type",outNewPath);
    if (!isForceOverwrite){
        _return_check(outNewPathType==kPathType_notExist,
                      kSyncClient_overwriteNewPathError,"%s already exists, overwrite",outNewPath);
    }
    if (isSamePath)
        _return_check(isForceOverwrite,kSyncClient_overwriteNewPathError,
                      "%s same path, overwrite","oldPath outNewPath");
    if (threadNum>1)
        printf("muti-thread parallel: opened, threadNum: %d\n",(int)threadNum);
    else
        printf("muti-thread parallel: closed\n");
    
    hpatch_TPathType oldPathType;
    if (0!=strcmp(oldPath,"")){ // isOldPathInputEmpty
        _return_check(hpatch_getPathStat(oldPath,&oldPathType,0),
                      kSyncClient_oldPathTypeError,"get oldPath %s type",oldPath);
        _return_check((oldPathType!=kPathType_notExist),
                      kSyncClient_oldPathTypeError,"oldPath %s not exist",oldPath);
    }else{ //same as empty file
        oldPathType=kPathType_file;
    }
    const bool oldIsDir=(oldPathType==kPathType_dir);
    
    int result=kSyncClient_ok;
    hpatch_BOOL newIsDir=hpatch_FALSE;
    result=checkNewSyncInfoType_by_file(newSyncInfoFile,&newIsDir);
    _return_check(result==kSyncClient_ok,result,"check newSyncInfoFile:%s ",newSyncInfoFile);
    if (!newIsDir)
        _return_check(!hpatch_getIsDirName(outNewPath),
                      kSyncClient_newPathTypeError,"outNewPath must fileName:%s ",outNewPath);
    
    char _newTempName[hpatch_kPathMaxSize];
    if (isSamePath){
        if (oldIsDir) _return_check(newIsDir,kSyncClient_overwriteNewPathError,
                                    "can not use file overwrite oldDirectory%s", "");
        if (!oldIsDir) _return_check(!newIsDir,kSyncClient_overwriteNewPathError,
                                     "can not use directory overwrite oldFile%s", "");
        _return_check(hpatch_getTempPathName(outNewPath,_newTempName,_newTempName+sizeof(_newTempName)),
                      kSyncClient_tempFileError,"getTempPathName(\"%s\")",outNewPath);
        printf("NOTE: temp outNewPath will be move to oldPath after sync_patch!\n");
        outNewPath=_newTempName;
    }
#if (_IS_NEED_DIR_DIFF_PATCH)
    if (newIsDir)
        result=sync_patch_2dir(outNewPath,oldPath,isSamePath,oldIsDir,
                               ignoreOldPathList,newSyncInfoFile,newSyncDataFile_url,
                               checksumSet,kMaxOpenFileNumber,threadNum);
    else
#endif
        result=sync_patch_2file(outNewPath,oldPath,isSamePath,oldIsDir,
                                ignoreOldPathList,newSyncInfoFile,newSyncDataFile_url,
                                checksumSet,kMaxOpenFileNumber,threadNum);
    double time1=clock_s();
    printf("\nsync_patch_%s2%s time: %.3f s\n\n",oldIsDir?"dir":"file",newIsDir?"dir":"file",(time1-time0));
    return result;
}

#if (_IS_NEED_DIR_DIFF_PATCH)
struct DirPathIgnoreListener:public CDirPathIgnore,IDirPathIgnore{
    DirPathIgnoreListener(const std::vector<std::string>& ignorePathList,bool isPrintIgnore=true)
    :CDirPathIgnore(ignorePathList,isPrintIgnore){}
    //IDirPathIgnore
    virtual bool isNeedIgnore(const std::string& path,size_t rootPathNameLen){
        return CDirPathIgnore::isNeedIgnore(path,rootPathNameLen);
    }
};
#endif

#if (_IS_NEED_DIR_DIFF_PATCH)
bool getManifest(TManifest& out_manifest,const char* _rootPath,bool rootPathIsDir,
                 const std::vector<std::string>& ignorePathList){
    try {
        std::string rootPath(_rootPath);
        if (rootPathIsDir) assignDirTag(rootPath);
        DirPathIgnoreListener pathIgnore(ignorePathList);
        get_manifest(&pathIgnore,rootPath,out_manifest);
        return true;
    } catch (...) {
        return false;
    }
}
#endif

int sync_patch_2file(const char* outNewFile,const char* oldPath,bool isSamePath,bool oldIsDir,
                     const std::vector<std::string>& ignoreOldPathList,
                     const char* newSyncInfoFile,const char* newSyncDataFile_url,
                     TSyncPatchChecksumSet checksumSet,size_t kMaxOpenFileNumber,size_t threadNum){
#if (_IS_NEED_DIR_DIFF_PATCH)
    std::string _oldPath(oldPath); if (oldIsDir) assignDirTag(_oldPath); oldPath=_oldPath.c_str();
#endif
    printf(  "\nin old %s: \"",oldIsDir?"dir ":"file"); hpatch_printPath_utf8(oldPath); printf("\"\n");
#if (_IS_NEED_DIR_DIFF_PATCH)
    TManifest oldManifest;
    if (oldIsDir){
        _return_check(getManifest(oldManifest,oldPath,oldIsDir,ignoreOldPathList),
                      kSyncClient_oldDirFilesError,"open oldPath: %s",oldPath);
        _oldPath=oldManifest.rootPath.c_str();
    }
#endif
    printf(    "in .hsyni  : \""); hpatch_printPath_utf8(newSyncInfoFile);
    printf("\"\nin .hsynd  : \""); hpatch_printPath_utf8(newSyncDataFile_url);
    printf("\"\nout file   : \""); hpatch_printPath_utf8(outNewFile);
    printf("\"\n");

    ISyncPatchListener listener; memset(&listener,0,sizeof(listener));
    listener.checksumSet=checksumSet;
    listener.findChecksumPlugin=_findChecksumPlugin;
    listener.findDecompressPlugin=_findDecompressPlugin;
    listener.needSyncInfo=_needSyncInfo;
    _return_check(openNewSyncDataByUrl(&listener,newSyncDataFile_url),
                  kSyncClient_openSyncDataError,"open newSyncData: %s",newSyncDataFile_url);
    int result=kSyncClient_ok;
#if (_IS_NEED_DIR_DIFF_PATCH)
    if (oldIsDir){
        result=sync_patch_dir2file(&listener,outNewFile,oldManifest,newSyncInfoFile,
                                   kMaxOpenFileNumber,(int)threadNum);
    }else
#endif
    {
        assert(!oldIsDir);
        result=sync_patch_file2file(&listener,outNewFile,oldPath,newSyncInfoFile,(int)threadNum);
    }
    if (isSamePath){
        // 1. patch to newTempName
        // 2. if patch ok    then  { delelte oldPath; rename newTempName to oldPath; }
        //    if patch error then  { delelte newTempName; }
        if (result==kSyncClient_ok){
            _return_check(hpatch_removeFile(oldPath),kSyncClient_deleteFileError,
                          "remove oldFile \"%s\"",oldPath);
            _return_check(hpatch_renamePath(outNewFile,oldPath),kSyncClient_renameFileError,
                          "rename \"%s\" to oldFile",outNewFile);
            printf("outNewPath temp file renamed to oldPath name!\n");
        }else{ //fail
            if (!hpatch_removeFile(outNewFile)){
                printf("WARNING: can't remove temp file \"");
                hpatch_printPath_utf8(outNewFile); printf("\"\n");
            }
        }
    }
    _return_check(closeNewSyncData(&listener),(result!=kSyncClient_ok)?result:kSyncClient_closeSyncDataError,
                  "close newSyncData: %s",newSyncDataFile_url);
    return result;
}

#if (_IS_NEED_DIR_DIFF_PATCH)
        
struct TDirSyncPatchListener:public IDirSyncPatchListener{
    bool                isPatchToNewTempDir;
    const TManifest*    oldManifest;
    const std::string*  newDirRoot;
    std::string         oldPathTemp;
};
static const char* TDirSyncPatchListener_getOldPathByNewPath(TDirSyncPatchListener* self,const char* newPath){
    const size_t newPathLen=strlen(newPath);
    const size_t newDirRootLen=self->newDirRoot->size();
    assert(newPathLen>=newDirRootLen);
    assert(0==memcmp(newPath,self->newDirRoot->c_str(),newDirRootLen));
    self->oldPathTemp.assign(self->oldManifest->rootPath);
    self->oldPathTemp.insert(self->oldPathTemp.end(),newPath+newDirRootLen,newPath+newPathLen);
    return self->oldPathTemp.c_str();
}
static const char* TDirSyncPatchListener_getOldPathByIndex(TDirSyncPatchListener* self,size_t oldIndex){
    assert(oldIndex<self->oldManifest->pathList.size());
    return self->oldManifest->pathList[oldIndex].c_str();
}
        
static hpatch_BOOL _dirSyncPatchFinish(IDirSyncPatchListener* listener,hpatch_BOOL isPatchSuccess,
                                       const TNewDataSyncInfo* newSyncInfo,TNewDirOutput* newDirOutput){
    TDirSyncPatchListener* self=(TDirSyncPatchListener*)listener->patchImport;
    if (!self->isPatchToNewTempDir) return hpatch_TRUE;
    
    hpatch_BOOL result=hpatch_TRUE;
    assert(self->isPatchToNewTempDir);
    // 1. patch all new to newTempDir
    // 2. if patch ok then  {
    //        set execute tags in newTempDir;
    //        delete file in oldManifest; //WARNING
    //        delete dir in oldManifest;  //not check
    //        move all files and dir in newTempDir to oldDir;
    //        delete newTempDir; }
    //    if patch error then  {
    //        delelte all in newTempDir;//not check
    //        delete newTempDir; }
    if (isPatchSuccess){
        {//set execute tags in newTempDir
            IDirPathList executeList;
            TNewDirOutput_getExecuteList(newDirOutput,&executeList);
            if (!_dirPatch_setIsExecuteFile(&executeList))
                result=hpatch_FALSE;
        }
        {//move new to old:
            IDirPathMove dirPathMove;
            dirPathMove.importMove=self;
            dirPathMove.getDstPathBySrcPath=
                    (IDirPathMove_getDstPathBySrcPath)TDirSyncPatchListener_getOldPathByNewPath;
            TNewDirOutput_getNewDirPathList(newDirOutput,&dirPathMove.srcPathList);
            dirPathMove.dstPathList.import=self;
            dirPathMove.dstPathList.pathCount=self->oldManifest->pathList.size();
            dirPathMove.dstPathList.getPathNameByIndex=
                    (IDirPathList_getPathNameByIndex)TDirSyncPatchListener_getOldPathByIndex;
            if (!_moveNewToOld(&dirPathMove))
                result=hpatch_FALSE;
        }
    }
    
    { //remove all temp file and dir
        IDirPathList newPathList;
        TNewDirOutput_getNewDirPathList(newDirOutput,&newPathList);
        deleteAllInPathList(&newPathList);
    }
    {//check remove newTempDir result
        const char* newTempDir=TNewDirOutput_getNewPathRoot(newDirOutput);
        if (!_isPathNotExist(newTempDir)){
            result=hpatch_FALSE;
            fprintf(stderr,"can't delete newTempDir \"");
            hpatch_printStdErrPath_utf8(newTempDir); fprintf(stderr,"\"  ERROR!\n");
        }
    }
    return result;
}

int  sync_patch_2dir(const char* outNewDir,const char* oldPath,bool isSamePath,bool oldIsDir,
                     const std::vector<std::string>& ignoreOldPathList,
                     const char* newSyncInfoFile,const char* newSyncDataFile_url,
                     TSyncPatchChecksumSet checksumSet,size_t kMaxOpenFileNumber,size_t threadNum){
    std::string _outNewDir(outNewDir); assignDirTag(_outNewDir); outNewDir=_outNewDir.c_str();
    std::string _oldPath(oldPath); if (oldIsDir) assignDirTag(_oldPath); oldPath=_oldPath.c_str();
    printf(  "\nin old %s: \"",oldIsDir?"dir ":"file"); hpatch_printPath_utf8(oldPath); printf("\"\n");
    TManifest oldManifest;
    if (oldIsDir){
        _return_check(getManifest(oldManifest,oldPath,oldIsDir,ignoreOldPathList),
                      kSyncClient_oldDirFilesError,"open oldPath: %s",oldPath);
        _oldPath=oldManifest.rootPath.c_str();
    }
    printf(    "in .hsyni  : \""); hpatch_printPath_utf8(newSyncInfoFile);
    printf("\"\nin .hsynd  : \""); hpatch_printPath_utf8(newSyncDataFile_url);
    printf("\"\nout dir    : \""); hpatch_printPath_utf8(outNewDir);
    printf("\"\n");
    
    IDirPatchListener     defaultPatchDirlistener={0,_makeNewDir,_copySameFile,_openNewFile,_closeNewFile};
    TDirSyncPatchListener listener; memset(&listener,0,sizeof(listener));
    listener.checksumSet=checksumSet;
    listener.findChecksumPlugin=_findChecksumPlugin;
    listener.findDecompressPlugin=_findDecompressPlugin;
    listener.needSyncInfo=_needSyncInfo;
    
    listener.patchImport=&listener;
    listener.isPatchToNewTempDir=isSamePath;
    listener.newDirRoot=&_outNewDir;
    listener.oldManifest=&oldManifest;
    listener.patchBegin=0;
    listener.patchFinish=_dirSyncPatchFinish;
    
    _return_check(openNewSyncDataByUrl(&listener,newSyncDataFile_url),
                  kSyncClient_openSyncDataError,"open newSyncData: %s",newSyncDataFile_url);
    int result=sync_patch_fileOrDir2dir(&defaultPatchDirlistener,&listener, outNewDir,oldManifest,
                                        newSyncInfoFile, kMaxOpenFileNumber,(int)threadNum);
    _return_check(closeNewSyncData(&listener),(result!=kSyncClient_ok)?result:kSyncClient_closeSyncDataError,
                  "close newSyncData: %s",newSyncDataFile_url);
    return result;
}
#endif


#if (_IS_NEED_DOWNLOAD_EMULATION)
#else
int downloadNewSyncInfoFile(const char* newSyncInfoFile_url,const char* outNewSyncInfoFile){
    int result=kSyncClient_ok;
    hpatch_TFileStreamOutput out_stream;
    hpatch_TFileStreamOutput_init(&out_stream);
    if (!hpatch_TFileStreamOutput_open(&out_stream,outNewSyncInfoFile,(hpatch_StreamPos_t)(-1)))
        return kSyncClient_newSyncInfoFileCreateError;
    if (downloadNewSyncInfoFromUrl(newSyncInfoFile_url,&out_stream.base))
        out_stream.base.streamSize=out_stream.out_length;
    else
        result=kSyncClient_newSyncInfoFileDownloadError;
    if (!hpatch_TFileStreamOutput_close(&out_stream)){
        if (result==kSyncClient_ok)
            result=kSyncClient_newSyncInfoFileCloseError;
    }
    return result;
}
#endif
