/*
 *  Copyright (c) 2011 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "udp_socket2_manager_windows.h"

#include <assert.h>
#include <stdio.h>

#include "aligned_malloc.h"
#include "udp_socket2_windows.h"

namespace webrtc {
WebRtc_UWord32 UdpSocket2ManagerWindows::_numOfActiveManagers = 0;
bool UdpSocket2ManagerWindows::_wsaInit = false;

UdpSocket2ManagerWindows::UdpSocket2ManagerWindows()
    : UdpSocketManager(),
      _id(-1),
      _stopped(false),
      _init(false),
      _pCrit(CriticalSectionWrapper::CreateCriticalSection()),
      _ioCompletionHandle(NULL),
      _numActiveSockets(0),
      _event(EventWrapper::Create())
{
    _managerNumber = _numOfActiveManagers++;

    if(_numOfActiveManagers == 1)
    {
        WORD wVersionRequested = MAKEWORD(2, 2);
        WSADATA wsaData;
        _wsaInit = WSAStartup(wVersionRequested, &wsaData) == 0;
        // TODO (hellner): seems safer to use RAII for this. E.g. what happens
        //                 if a UdpSocket2ManagerWindows() created and destroyed
        //                 without being initialized.
    }
}

UdpSocket2ManagerWindows::~UdpSocket2ManagerWindows()
{
    WEBRTC_TRACE(kTraceDebug, kTraceTransport, _id,
                 "UdpSocket2ManagerWindows(%d)::~UdpSocket2ManagerWindows()",
                 _managerNumber);

    if(_init)
    {
        _pCrit->Enter();
        if(_numActiveSockets)
        {
            _pCrit->Leave();
            _event->Wait(INFINITE);
        }
        else
        {
            _pCrit->Leave();
        }
        StopWorkerThreads();

        // All threads are stopped. Safe to delete them.
        ListItem* pItem = NULL;
        UdpSocket2WorkerWindows* pWorker;
        while((pItem = _workerThreadsList.First()) != NULL)
        {
            pWorker = (UdpSocket2WorkerWindows*)pItem->GetItem();
            delete pWorker;
            _workerThreadsList.PopFront();
        }

        _ioContextPool.Free();

        _numOfActiveManagers--;
        if(_ioCompletionHandle)
        {
            CloseHandle(_ioCompletionHandle);
        }
        if (_numOfActiveManagers == 0)
        {
            if(_wsaInit)
            {
                WSACleanup();
            }
        }
    }
    if(_pCrit)
    {
        delete _pCrit;
    }
    if(_event)
    {
        delete _event;
    }
}

bool UdpSocket2ManagerWindows::Init(WebRtc_Word32 id,
                                    WebRtc_UWord8& numOfWorkThreads) {
  CriticalSectionScoped cs(_pCrit);
  if ((_id != -1) || (_numOfWorkThreads != 0)) {
      assert(_id != -1);
      assert(_numOfWorkThreads != 0);
      return false;
  }
  _id = id;
  _numOfWorkThreads = numOfWorkThreads;
  return true;
}

WebRtc_Word32 UdpSocket2ManagerWindows::ChangeUniqueId(const WebRtc_Word32 id)
{
    _id = id;
    return 0;
}

bool UdpSocket2ManagerWindows::Start()
{
    WEBRTC_TRACE(kTraceDebug, kTraceTransport, _id,
                 "UdpSocket2ManagerWindows(%d)::Start()",_managerNumber);
    if(!_init)
    {
        StartWorkerThreads();
    }

    if(!_init)
    {
        return false;
    }
    _pCrit->Enter();
    // Start worker threads.
    _stopped = false;
    WebRtc_Word32 i = 0;
    WebRtc_Word32 error = 0;
    ListItem* pItem = _workerThreadsList.First();
    UdpSocket2WorkerWindows* pWorker;
    while(pItem != NULL && !error)
    {
        pWorker = (UdpSocket2WorkerWindows*)pItem->GetItem();
        if(!pWorker->Start())
            error = 1;
        pItem = _workerThreadsList.Next(pItem);
    }
    if(error)
    {
        WEBRTC_TRACE(
            kTraceError,
            kTraceTransport,
            _id,
            "UdpSocket2ManagerWindows(%d)::Start() error starting worker\
 threads",
            _managerNumber);
        _pCrit->Leave();
        return false;
    }
    _pCrit->Leave();
    return true;
}

bool UdpSocket2ManagerWindows::StartWorkerThreads()
{
    if(!_init)
    {
        _pCrit->Enter();

        _ioCompletionHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL,
                                                     0, 0);
        if(_ioCompletionHandle == NULL)
        {
            WebRtc_Word32 error = GetLastError();
            WEBRTC_TRACE(
                kTraceError,
                kTraceTransport,
                _id,
                "UdpSocket2ManagerWindows(%d)::StartWorkerThreads()"
                "_ioCompletioHandle == NULL: error:%d",
                _managerNumber,error);
            _pCrit->Leave();
            return false;
        }

        // Create worker threads.
        WebRtc_UWord32 i = 0;
        WebRtc_Word32 error = 0;
        while(i < _numOfWorkThreads && !error)
        {
            UdpSocket2WorkerWindows* pWorker =
                new UdpSocket2WorkerWindows(_ioCompletionHandle);
            if(pWorker == NULL)
            {
                error = 1;
                break;
            }
            if(pWorker->Init())
            {
                error = 1;
                delete pWorker;
                break;
            }
            _workerThreadsList.PushFront(pWorker);
            i++;
        }
        if(error)
        {
            WEBRTC_TRACE(
                kTraceError,
                kTraceTransport,
                _id,
                "UdpSocket2ManagerWindows(%d)::StartWorkerThreads() error "
                "creating work threads",
                _managerNumber);
            // Delete worker threads.
            ListItem* pItem = NULL;
            UdpSocket2WorkerWindows* pWorker;
            while((pItem = _workerThreadsList.First()) != NULL)
            {
                pWorker = (UdpSocket2WorkerWindows*)pItem->GetItem();
                delete pWorker;
                _workerThreadsList.PopFront();
            }
            _pCrit->Leave();
            return false;
        }
        if(_ioContextPool.Init())
        {
            WEBRTC_TRACE(
                kTraceError,
                kTraceTransport,
                _id,
                "UdpSocket2ManagerWindows(%d)::StartWorkerThreads() error "
                "initiating _ioContextPool",
                _managerNumber);
            _pCrit->Leave();
            return false;
        }
        _init = true;
        WEBRTC_TRACE(
            kTraceDebug,
            kTraceTransport,
            _id,
            "UdpSocket2ManagerWindows::StartWorkerThreads %d number of work "
            "threads created and initialized",
            _numOfWorkThreads);
        _pCrit->Leave();
    }
    return true;
}

bool UdpSocket2ManagerWindows::Stop()
{
    WEBRTC_TRACE(kTraceDebug, kTraceTransport, _id,
                 "UdpSocket2ManagerWindows(%d)::Stop()",_managerNumber);

    if(!_init)
    {
        return false;
    }
    _pCrit->Enter();
    _stopped = true;
    if(_numActiveSockets)
    {
        WEBRTC_TRACE(
            kTraceError,
            kTraceTransport,
            _id,
            "UdpSocket2ManagerWindows(%d)::Stop() there is still active\
 sockets",
            _managerNumber);
        _pCrit->Leave();
        return false;
    }
    // No active sockets. Stop all worker threads.
    bool result = StopWorkerThreads();
    _pCrit->Leave();
    return result;
}

bool UdpSocket2ManagerWindows::StopWorkerThreads()
{
    WebRtc_Word32 error = 0;
    WEBRTC_TRACE(
        kTraceDebug,
        kTraceTransport,
        _id,
        "UdpSocket2ManagerWindows(%d)::StopWorkerThreads() Worker\
 threadsStoped, numActicve Sockets=%d",
        _managerNumber,
        _numActiveSockets);
    UdpSocket2WorkerWindows* pWorker;
    ListItem* pItem = _workerThreadsList.First();

    // Set worker threads to not alive so that they will stop calling
    // UdpSocket2WorkerWindows::Run().
    while(pItem != NULL)
    {
        pWorker = (UdpSocket2WorkerWindows*)pItem->GetItem();
        pWorker->SetNotAlive();
        pItem = _workerThreadsList.Next(pItem);
    }
    // Release all threads waiting for GetQueuedCompletionStatus(..).
    if(_ioCompletionHandle)
    {
        WebRtc_UWord32 i = 0;
        for(i = 0; i < _workerThreadsList.GetSize(); i++)
        {
            PostQueuedCompletionStatus(_ioCompletionHandle, 0 ,0 , NULL);
        }
    }
    pItem = _workerThreadsList.First();

    while(pItem != NULL)
    {
        pWorker = (UdpSocket2WorkerWindows*)pItem->GetItem();
        if(pWorker->Stop() == false)
        {
            error = -1;
            WEBRTC_TRACE(kTraceWarning,  kTraceTransport, -1,
                         "failed to stop worker thread");
        }
        pItem = _workerThreadsList.Next(pItem);
    }

    if(error)
    {
        WEBRTC_TRACE(
            kTraceError,
            kTraceTransport,
            _id,
            "UdpSocket2ManagerWindows(%d)::StopWorkerThreads() error stopping\
 worker threads",
            _managerNumber);
        return false;
    }
    return true;
}

bool UdpSocket2ManagerWindows::AddSocketPrv(UdpSocket2Windows* s)
{
    WEBRTC_TRACE(kTraceDebug, kTraceTransport, _id,
                 "UdpSocket2ManagerWindows(%d)::AddSocketPrv()",_managerNumber);
    if(!_init)
    {
        WEBRTC_TRACE(
            kTraceError,
            kTraceTransport,
            _id,
            "UdpSocket2ManagerWindows(%d)::AddSocketPrv() manager not\
 initialized",
            _managerNumber);
        return false;
    }
    _pCrit->Enter();
    if(s == NULL)
    {
        WEBRTC_TRACE(
            kTraceError,
            kTraceTransport,
            _id,
            "UdpSocket2ManagerWindows(%d)::AddSocketPrv() socket == NULL",
            _managerNumber);
        _pCrit->Leave();
        return false;
    }
    if(s->GetFd() == NULL || s->GetFd() == INVALID_SOCKET)
    {
        WEBRTC_TRACE(
            kTraceError,
            kTraceTransport,
            _id,
            "UdpSocket2ManagerWindows(%d)::AddSocketPrv() socket->GetFd() ==\
 %d",
            _managerNumber,
            (WebRtc_Word32)s->GetFd());
        _pCrit->Leave();
        return false;

    }
    _ioCompletionHandle = CreateIoCompletionPort((HANDLE)s->GetFd(),
                                                 _ioCompletionHandle,
                                                 (ULONG_PTR)(s), 0);
    if(_ioCompletionHandle == NULL)
    {
        WebRtc_Word32 error = GetLastError();
        WEBRTC_TRACE(
            kTraceError,
            kTraceTransport,
            _id,
            "UdpSocket2ManagerWindows(%d)::AddSocketPrv() Error adding to IO\
 completion: %d",
            _managerNumber,
            error);
        _pCrit->Leave();
        return false;
    }
    _numActiveSockets++;
    _pCrit->Leave();
    return true;
}
bool UdpSocket2ManagerWindows::RemoveSocketPrv(UdpSocket2Windows* s)
{
    if(!_init)
    {
        return false;
    }
    _pCrit->Enter();
    _numActiveSockets--;
    if(_numActiveSockets == 0)
    {
        _event->Set();
    }
    _pCrit->Leave();
    return true;
}

PerIoContext* UdpSocket2ManagerWindows::PopIoContext()
{
    if(!_init)
    {
        return NULL;
    }

    PerIoContext* pIoC = NULL;
    if(!_stopped)
    {
        pIoC = _ioContextPool.PopIoContext();
    }else
    {
        WEBRTC_TRACE(
            kTraceError,
            kTraceTransport,
            _id,
            "UdpSocket2ManagerWindows(%d)::PopIoContext() Manager Not started",
            _managerNumber);
    }
    return pIoC;
}

WebRtc_Word32 UdpSocket2ManagerWindows::PushIoContext(PerIoContext* pIoContext)
{
    return _ioContextPool.PushIoContext(pIoContext);
}

IoContextPool::IoContextPool()
    : _pListHead(NULL),
      _init(false),
      _size(0),
      _inUse(0)
{
}

IoContextPool::~IoContextPool()
{
    Free();
    assert(_size.Value() == 0);
    AlignedFree(_pListHead);
}

WebRtc_Word32 IoContextPool::Init(WebRtc_UWord32 /*increaseSize*/)
{
    if(_init)
    {
        return 0;
    }

    _pListHead = (PSLIST_HEADER)AlignedMalloc(sizeof(SLIST_HEADER),
                                              MEMORY_ALLOCATION_ALIGNMENT);
    if(_pListHead == NULL)
    {
        return -1;
    }
    InitializeSListHead(_pListHead);
    _init = true;
    return 0;
}

PerIoContext* IoContextPool::PopIoContext()
{
    if(!_init)
    {
        return NULL;
    }

    PSLIST_ENTRY pListEntry = InterlockedPopEntrySList(_pListHead);
    if(pListEntry == NULL)
    {
        IoContextPoolItem* item = (IoContextPoolItem*)
            AlignedMalloc(
                sizeof(IoContextPoolItem),
                MEMORY_ALLOCATION_ALIGNMENT);
        if(item == NULL)
        {
            return NULL;
        }
        memset(&item->payload.ioContext,0,sizeof(PerIoContext));
        item->payload.base = item;
        pListEntry = &(item->itemEntry);
        ++_size;
    }
    ++_inUse;
    return &((IoContextPoolItem*)pListEntry)->payload.ioContext;
}

WebRtc_Word32 IoContextPool::PushIoContext(PerIoContext* pIoContext)
{
    // TODO (hellner): Overlapped IO should be completed at this point. Perhaps
    //                 add an assert?
    const bool overlappedIOCompleted = HasOverlappedIoCompleted(
        (LPOVERLAPPED)pIoContext);

    IoContextPoolItem* item = ((IoContextPoolItemPayload*)pIoContext)->base;

    const WebRtc_Word32 usedItems = --_inUse;
    const WebRtc_Word32 totalItems = _size.Value();
    const WebRtc_Word32 freeItems = totalItems - usedItems;
    if(freeItems < 0)
    {
        assert(false);
        AlignedFree(item);
        return -1;
    }
    if((freeItems >= totalItems>>1) &&
        overlappedIOCompleted)
    {
        AlignedFree(item);
        --_size;
        return 0;
    }
    InterlockedPushEntrySList(_pListHead, &(item->itemEntry));
    return 0;
}

WebRtc_Word32 IoContextPool::Free()
{
    if(!_init)
    {
        return 0;
    }

    WebRtc_Word32 itemsFreed = 0;
    PSLIST_ENTRY pListEntry = InterlockedPopEntrySList(_pListHead);
    while(pListEntry != NULL)
    {
        IoContextPoolItem* item = ((IoContextPoolItem*)pListEntry);
        AlignedFree(item);
        --_size;
        itemsFreed++;
        pListEntry = InterlockedPopEntrySList(_pListHead);
    }
    return itemsFreed;
}

WebRtc_Word32 UdpSocket2WorkerWindows::_numOfWorkers = 0;

UdpSocket2WorkerWindows::UdpSocket2WorkerWindows(HANDLE ioCompletionHandle)
    : _ioCompletionHandle(ioCompletionHandle),
      _pThread(NULL),
      _init(false)
{
    _workerNumber = _numOfWorkers++;
    WEBRTC_TRACE(kTraceMemory,  kTraceTransport, -1,
                 "UdpSocket2WorkerWindows created");
}

UdpSocket2WorkerWindows::~UdpSocket2WorkerWindows()
{
    if(_pThread)
    {
        delete _pThread;
    }
    WEBRTC_TRACE(kTraceMemory,  kTraceTransport, -1,
                 "UdpSocket2WorkerWindows deleted");
}

bool UdpSocket2WorkerWindows::Start()
{
    unsigned int id = 0;
    WEBRTC_TRACE(kTraceStateInfo,  kTraceTransport, -1,
                 "Start UdpSocket2WorkerWindows");
    return _pThread->Start(id);
}

bool UdpSocket2WorkerWindows::Stop()
{
    WEBRTC_TRACE(kTraceStateInfo,  kTraceTransport, -1,
                 "Stop UdpSocket2WorkerWindows");
    return _pThread->Stop();
}

void UdpSocket2WorkerWindows::SetNotAlive()
{
    WEBRTC_TRACE(kTraceStateInfo,  kTraceTransport, -1,
                 "SetNotAlive UdpSocket2WorkerWindows");
    _pThread->SetNotAlive();
}

WebRtc_Word32 UdpSocket2WorkerWindows::Init()
{
    if(!_init)
    {
        const WebRtc_Word8* threadName = "UdpSocket2ManagerWindows_thread";
        _pThread = ThreadWrapper::CreateThread(Run, this, kRealtimePriority,
                                               threadName);
        if(_pThread == NULL)
        {
            WEBRTC_TRACE(
                kTraceError,
                kTraceTransport,
                -1,
                "UdpSocket2WorkerWindows(%d)::Init(), error creating thread!",
                _workerNumber);
            return -1;
        }
        _init = true;
    }
    return 0;
}

bool UdpSocket2WorkerWindows::Run(ThreadObj obj)
{
    UdpSocket2WorkerWindows* pWorker =
        static_cast<UdpSocket2WorkerWindows*>(obj);
    return pWorker->Process();
}

// Process should always return true. Stopping the worker threads is done in
// the UdpSocket2ManagerWindows::StopWorkerThreads() function.
bool UdpSocket2WorkerWindows::Process()
{
    WebRtc_Word32 success = 0;
    DWORD ioSize = 0;
    UdpSocket2Windows* pSocket = NULL;
    PerIoContext* pIOContext = 0;
    OVERLAPPED* pOverlapped = 0;
    success = GetQueuedCompletionStatus(_ioCompletionHandle,
                                        &ioSize,
                                       (ULONG_PTR*)&pSocket, &pOverlapped, 200);

    WebRtc_UWord32 error = 0;
    if(!success)
    {
        error = GetLastError();
        if(error == WAIT_TIMEOUT)
        {
            return true;
        }
        // This may happen if e.g. PostQueuedCompletionStatus() has been called.
        // The IO context still needs to be reclaimed or re-used which is done
        // in UdpSocket2Windows::IOCompleted(..).
    }
    if(pSocket == NULL)
    {
        WEBRTC_TRACE(
            kTraceDebug,
            kTraceTransport,
            -1,
            "UdpSocket2WorkerWindows(%d)::Process(), pSocket == 0, end thread",
            _workerNumber);
        return true;
    }
    pIOContext = (PerIoContext*)pOverlapped;
    pSocket->IOCompleted(pIOContext,ioSize,error);
    return true;
}
} // namespace webrtc
