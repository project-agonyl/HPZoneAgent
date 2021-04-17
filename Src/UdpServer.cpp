﻿/*
 * Copyright: JessMA Open Source (ldcsaa@gmail.com)
 *
 * Author	: Bruce Liang
 * Website	: https://github.com/ldcsaa
 * Project	: https://github.com/ldcsaa/HP-Socket
 * Blog		: http://www.cnblogs.com/ldcsaa
 * Wiki		: http://www.oschina.net/p/hp-socket
 * QQ Group	: 44636872, 75375912
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
 
#include "stdafx.h"
#include "UdpServer.h"
#include "Common/WaitFor.h"

#ifdef _UDP_SUPPORT

#include <malloc.h>

const CInitSocket CUdpServer::sm_wsSocket;

EnHandleResult CUdpServer::TriggerFireAccept(TUdpSocketObj* pSocketObj)
{
	EnHandleResult rs = TRIGGER(FireAccept(pSocketObj));

	return rs;
}

EnHandleResult CUdpServer::TriggerFireReceive(TUdpSocketObj* pSocketObj, TUdpBufferObj* pBufferObj)
{
	EnHandleResult rs = (EnHandleResult)HR_CLOSED;

	WSABUF& buff = pBufferObj->buff;

	if(!::IsUdpCloseNotify((BYTE*)buff.buf, buff.len))
	{
		if(TUdpSocketObj::IsValid(pSocketObj))
		{
			CReentrantReadLock locallock(pSocketObj->csRecv);

			if(TUdpSocketObj::IsValid(pSocketObj))
			{
				rs = TRIGGER(FireReceive(pSocketObj, (BYTE*)buff.buf, buff.len));
			}
		}
	}

	return rs;
}

EnHandleResult CUdpServer::TriggerFireSend(TUdpSocketObj* pSocketObj, TUdpBufferObj* pBufferObj)
{
	EnHandleResult rs = (EnHandleResult)HR_CLOSED;

	if(m_enOnSendSyncPolicy == OSSP_NONE)
		rs = TRIGGER(FireSend(pSocketObj, (BYTE*)pBufferObj->buff.buf, pBufferObj->buff.len));
	else
	{
		ASSERT(m_enOnSendSyncPolicy == OSSP_CLOSE);

		if(TUdpSocketObj::IsValid(pSocketObj))
		{
			CCriSecLock locallock(pSocketObj->csSend);

			if(TUdpSocketObj::IsValid(pSocketObj))
			{
				rs = TRIGGER(FireSend(pSocketObj, (BYTE*)pBufferObj->buff.buf, pBufferObj->buff.len));
			}
		}
	}

	if(rs == HR_ERROR)
	{
		TRACE("<S-CNNID: %Iu> OnSend() event should not return 'HR_ERROR' !!\n", pSocketObj->connID);
		ASSERT(FALSE);
	}

	if(pBufferObj->ReleaseSendCounter() == 0)
		AddFreeBufferObj(pBufferObj);

	return rs;
}

EnHandleResult CUdpServer::TriggerFireClose(TUdpSocketObj* pSocketObj, EnSocketOperation enOperation, int iErrorCode)
{
	CReentrantWriteLock locallock(pSocketObj->csRecv);
	return FireClose(pSocketObj, enOperation, iErrorCode);
}

void CUdpServer::SetLastError(EnSocketError code, LPCSTR func, int ec)
{
	m_enLastError = code;
	::SetLastError(ec);

	TRACE("%s --> Error: %d, EC: %d\n", func, code, ec);
}

BOOL CUdpServer::Start(LPCTSTR lpszBindAddress, USHORT usPort)
{
	if(!CheckParams() || !CheckStarting())
		return FALSE;

	PrepareStart();

	if(CreateListenSocket(lpszBindAddress, usPort))
		if(CreateCompletePort())
			if(CreateWorkerThreads())
				if(StartAccept())
				{
					m_enState = SS_STARTED;

					m_evWait.Reset();

					return TRUE;
				}

	EXECUTE_RESTORE_ERROR(Stop());

	return FALSE;
}

BOOL CUdpServer::CheckParams()
{
	if	((m_enSendPolicy >= SP_PACK && m_enSendPolicy <= SP_DIRECT)								&&
		(m_enOnSendSyncPolicy >= OSSP_NONE && m_enOnSendSyncPolicy <= OSSP_CLOSE)				&&
		((int)m_dwMaxConnectionCount > 0 && m_dwMaxConnectionCount <= MAX_CONNECTION_COUNT)		&&
		((int)m_dwWorkerThreadCount > 0 && m_dwWorkerThreadCount <= MAX_WORKER_THREAD_COUNT)	&&
		((int)m_dwFreeSocketObjLockTime >= 1000)												&&
		((int)m_dwFreeSocketObjPool >= 0)														&&
		((int)m_dwFreeBufferObjPool >= 0)														&&
		((int)m_dwFreeSocketObjHold >= 0)														&&
		((int)m_dwFreeBufferObjHold >= 0)														&&
		((int)m_dwMaxDatagramSize > 0 && m_dwMaxDatagramSize <= MAXIMUM_UDP_MAX_DATAGRAM_SIZE)	&&
		((int)m_dwPostReceiveCount > 0)															&&
		((int)m_dwDetectAttempts >= 0)															&&
		((int)m_dwDetectInterval >= 1000 || m_dwDetectInterval == 0)							)
		return TRUE;

	SetLastError(SE_INVALID_PARAM, __FUNCTION__, ERROR_INVALID_PARAMETER);
	return FALSE;
}

void CUdpServer::PrepareStart()
{
	m_bfActiveSockets.Reset(m_dwMaxConnectionCount);
	m_lsFreeSocket.Reset(m_dwFreeSocketObjPool);

	m_bfObjPool.SetItemCapacity(m_dwMaxDatagramSize);
	m_bfObjPool.SetPoolSize(m_dwFreeBufferObjPool);
	m_bfObjPool.SetPoolHold(m_dwFreeBufferObjHold);

	m_bfObjPool.Prepare();
}

BOOL CUdpServer::CheckStarting()
{
	CSpinLock locallock(m_csState);

	if(m_enState == SS_STOPPED)
		m_enState = SS_STARTING;
	else
	{
		SetLastError(SE_ILLEGAL_STATE, __FUNCTION__, ERROR_INVALID_STATE);
		return FALSE;
	}

	return TRUE;
}

BOOL CUdpServer::CheckStoping()
{
	if(m_enState != SS_STOPPED)
	{
		CSpinLock locallock(m_csState);

		if(HasStarted())
		{
			m_enState = SS_STOPPING;
			return TRUE;
		}
	}

	SetLastError(SE_ILLEGAL_STATE, __FUNCTION__, ERROR_INVALID_STATE);

	return FALSE;
}

BOOL CUdpServer::CreateListenSocket(LPCTSTR lpszBindAddress, USHORT usPort)
{
	BOOL isOK = FALSE;

	if(!lpszBindAddress || lpszBindAddress[0] == 0)
		lpszBindAddress = DEFAULT_IPV4_BIND_ADDRESS;

	HP_SOCKADDR addr;

	if(::sockaddr_A_2_IN(lpszBindAddress, usPort, addr))
	{
		m_usFamily = addr.family;
		m_soListen = socket(m_usFamily, SOCK_DGRAM, IPPROTO_UDP);

		if(m_soListen != INVALID_SOCKET)
		{
			ENSURE(::SSO_UDP_ConnReset(m_soListen, FALSE) == NO_ERROR);
			ENSURE(::SSO_ReuseAddress(m_soListen, m_enReusePolicy) == NO_ERROR);
			ENSURE(::SSO_NoBlock(m_soListen) == NO_ERROR);

			if(::bind(m_soListen, addr.Addr(), addr.AddrSize()) != SOCKET_ERROR)
			{
				if(TRIGGER(FirePrepareListen(m_soListen)) != HR_ERROR)
					isOK = TRUE;
				else
					SetLastError(SE_SOCKET_PREPARE, __FUNCTION__, ENSURE_ERROR_CANCELLED);
			}
			else
				SetLastError(SE_SOCKET_BIND, __FUNCTION__, ::WSAGetLastError());
		}
		else
			SetLastError(SE_SOCKET_CREATE, __FUNCTION__, ::WSAGetLastError());
	}
	else
		SetLastError(SE_SOCKET_CREATE, __FUNCTION__, ::WSAGetLastError());

	return isOK;
}

BOOL CUdpServer::CreateCompletePort()
{
	m_hCompletePort	= ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	
	if(m_hCompletePort == nullptr)
		SetLastError(SE_CP_CREATE, __FUNCTION__, ::GetLastError());

	return (m_hCompletePort != nullptr);
}

BOOL CUdpServer::CreateWorkerThreads()
{
	BOOL isOK = TRUE;

	for(DWORD i = 0; i < m_dwWorkerThreadCount; i++)
	{
		HANDLE hThread = (HANDLE)_beginthreadex(nullptr, 0, WorkerThreadProc, (LPVOID)this, 0, nullptr);
		
		if(hThread)
			m_vtWorkerThreads.push_back(hThread);
		else
		{
			SetLastError(SE_WORKER_THREAD_CREATE, __FUNCTION__, ::GetLastError());
			isOK = FALSE;
			break;
		}
	}

	return isOK;
}

BOOL CUdpServer::StartAccept()
{
	BOOL isOK = TRUE;

	if(::CreateIoCompletionPort((HANDLE)m_soListen, m_hCompletePort, m_soListen, 0))
	{
		m_iRemainPostReceives = m_dwPostReceiveCount;

		for(DWORD i = 0; i < m_dwPostReceiveCount; i++)
			ENSURE(::PostIocpAccept(m_hCompletePort));
	}
	else
	{
		SetLastError(SE_SOCKE_ATTACH_TO_CP, __FUNCTION__, ::GetLastError());
		isOK = FALSE;
	}

	return isOK;
}

BOOL CUdpServer::Stop()
{
	if(!CheckStoping())
		return FALSE;

	SendCloseNotify();

	CloseListenSocket();

	WaitForPostReceiveRelease();

	DisconnectClientSocket();
	WaitForClientSocketClose();
	WaitForWorkerThreadEnd();
	
	ReleaseClientSocket();

	FireShutdown();

	ReleaseFreeSocket();
	ReleaseFreeBuffer();

	CloseCompletePort();

	Reset();

	return TRUE;
}

void CUdpServer::Reset()
{
	m_tqDetect.Reset();
	m_phSocket.Reset();

	m_iRemainPostReceives	= 0;
	m_enState				= SS_STOPPED;
	m_usFamily				= AF_UNSPEC;

	m_evWait.Set();
}

void CUdpServer::SendCloseNotify()
{
	if(m_soListen == INVALID_SOCKET)
		return;

	DWORD size				 = 0;
	unique_ptr<CONNID[]> ids = m_bfActiveSockets.GetAllElementIndexes(size);

	if(size == 0)
		return;

	for(DWORD i = 0; i < size; i++)
	{
		CONNID connID			  = ids[i];
		TUdpSocketObj* pSocketObj = FindSocketObj(connID);

		if(TUdpSocketObj::IsValid(pSocketObj))
			::SendUdpCloseNotify(m_soListen, pSocketObj->remoteAddr);
	}

	::WaitWithMessageLoop(30);
}

void CUdpServer::CloseListenSocket()
{
	if(m_soListen == INVALID_SOCKET)
		return;

	::ManualCloseSocket(m_soListen);
	m_soListen = INVALID_SOCKET;

	::WaitWithMessageLoop(70);
}

void CUdpServer::DisconnectClientSocket()
{
	DWORD size					= 0;
	unique_ptr<CONNID[]> ids	= m_bfActiveSockets.GetAllElementIndexes(size);

	for(DWORD i = 0; i < size; i++)
		Disconnect(ids[i]);
}

void CUdpServer::ReleaseClientSocket()
{
	ENSURE(m_bfActiveSockets.IsEmpty());
	m_bfActiveSockets.Reset();

	CWriteLock locallock(m_csClientSocket);
	m_mpClientAddr.clear();
}

TUdpSocketObj* CUdpServer::GetFreeSocketObj(CONNID dwConnID)
{
	DWORD dwIndex;
	TUdpSocketObj* pSocketObj = nullptr;

	if(m_lsFreeSocket.TryLock(&pSocketObj, dwIndex))
	{
		if(::GetTimeGap32(pSocketObj->freeTime) >= m_dwFreeSocketObjLockTime)
			ENSURE(m_lsFreeSocket.ReleaseLock(nullptr, dwIndex));
		else
		{
			ENSURE(m_lsFreeSocket.ReleaseLock(pSocketObj, dwIndex));
			pSocketObj = nullptr;
		}
	}

	if(!pSocketObj) pSocketObj = CreateSocketObj();

	pSocketObj->Reset(dwConnID);

	return pSocketObj;
}

void CUdpServer::AddFreeSocketObj(CONNID dwConnID, EnSocketCloseFlag enFlag, EnSocketOperation enOperation, int iErrorCode, BOOL bNotify)
{
	AddFreeSocketObj(FindSocketObj(dwConnID), enFlag, enOperation, iErrorCode, bNotify);
}

void CUdpServer::AddFreeSocketObj(TUdpSocketObj* pSocketObj, EnSocketCloseFlag enFlag, EnSocketOperation enOperation, int iErrorCode, BOOL bNotify)
{
	if(!InvalidSocketObj(pSocketObj))
		return;

	CloseClientSocketObj(pSocketObj, enFlag, enOperation, iErrorCode, bNotify);

	{
		m_bfActiveSockets.Remove(pSocketObj->connID);

		CWriteLock locallock(m_csClientSocket);
		m_mpClientAddr.erase(&pSocketObj->remoteAddr);
	}

	if(pSocketObj->hTimer != nullptr)
		m_tqDetect.DeleteTimer(pSocketObj->hTimer);

	TUdpSocketObj::Release(pSocketObj);

	ReleaseGCSocketObj();
	
	if(!m_lsFreeSocket.TryPut(pSocketObj))
		m_lsGCSocket.PushBack(pSocketObj);
}

void CUdpServer::ReleaseGCSocketObj(BOOL bForce)
{
	::ReleaseGCObj(m_lsGCSocket, m_dwFreeSocketObjLockTime, bForce);
}

BOOL CUdpServer::InvalidSocketObj(TUdpSocketObj* pSocketObj)
{
	return TUdpSocketObj::InvalidSocketObj(pSocketObj);
}

void CUdpServer::AddClientSocketObj(CONNID dwConnID, TUdpSocketObj* pSocketObj, const HP_SOCKADDR& remoteAddr)
{
	ASSERT(FindSocketObj(dwConnID) == nullptr);

	if(IsNeedDetectConnection())
		pSocketObj->hTimer	= m_tqDetect.CreateTimer(DetectConnectionProc, pSocketObj, m_dwDetectInterval);

	pSocketObj->pHolder		= this;
	pSocketObj->connTime	= ::TimeGetTime();
	pSocketObj->activeTime	= pSocketObj->connTime;

	remoteAddr.Copy(pSocketObj->remoteAddr);
	pSocketObj->SetConnected();

	ENSURE(m_bfActiveSockets.ReleaseLock(dwConnID, pSocketObj));

	CWriteLock locallock(m_csClientSocket);
	m_mpClientAddr[&pSocketObj->remoteAddr]	= dwConnID;
}

void CUdpServer::ReleaseFreeSocket()
{
	m_lsFreeSocket.Clear();

	ReleaseGCSocketObj(TRUE);
	ENSURE(m_lsGCSocket.IsEmpty());
}

TUdpSocketObj* CUdpServer::CreateSocketObj()
{
	return TUdpSocketObj::Construct(m_phSocket, m_bfObjPool);
}

void CUdpServer::DeleteSocketObj(TUdpSocketObj* pSocketObj)
{
	TUdpSocketObj::Destruct(pSocketObj);
}

TUdpBufferObj* CUdpServer::GetFreeBufferObj(int iLen)
{
	ASSERT(iLen >= -1 && iLen <= (int)m_dwMaxDatagramSize);

	TUdpBufferObj* pBufferObj		= m_bfObjPool.PickFreeItem();;
	if(iLen < 0) iLen				= m_dwMaxDatagramSize;
	pBufferObj->buff.len			= iLen;
	pBufferObj->remoteAddr.family	= m_usFamily;
	pBufferObj->addrLen				= pBufferObj->remoteAddr.AddrSize();

	return pBufferObj;
}

void CUdpServer::AddFreeBufferObj(TUdpBufferObj* pBufferObj)
{
	m_bfObjPool.PutFreeItem(pBufferObj);
}

void CUdpServer::ReleaseFreeBuffer()
{
	m_bfObjPool.Clear();
}

TUdpSocketObj* CUdpServer::FindSocketObj(CONNID dwConnID)
{
	TUdpSocketObj* pSocketObj = nullptr;

	if(m_bfActiveSockets.Get(dwConnID, &pSocketObj) != TUdpSocketObjPtrPool::GR_VALID)
		pSocketObj = nullptr;

	return pSocketObj;
}

CONNID CUdpServer::FindConnectionID(const HP_SOCKADDR* pAddr)
{
	CONNID dwConnID = 0;

	CReadLock locallock(m_csClientSocket);

	TSockAddrMapCI it = m_mpClientAddr.find(pAddr);
	if(it != m_mpClientAddr.end())
		dwConnID = it->second;

	return dwConnID;
}

void CUdpServer::CloseClientSocketObj(TUdpSocketObj* pSocketObj, EnSocketCloseFlag enFlag, EnSocketOperation enOperation, int iErrorCode, BOOL bNotify)
{
	ASSERT(TUdpSocketObj::IsExist(pSocketObj));

	if(bNotify && m_soListen != INVALID_SOCKET)
		::SendUdpCloseNotify(m_soListen, pSocketObj->remoteAddr);

	if(enFlag == SCF_CLOSE)
		TriggerFireClose(pSocketObj, SO_CLOSE, SE_OK);
	else if(enFlag == SCF_ERROR)
		TriggerFireClose(pSocketObj, enOperation, iErrorCode);
}

BOOL CUdpServer::GetListenAddress(TCHAR lpszAddress[], int& iAddressLen, USHORT& usPort)
{
	ASSERT(lpszAddress != nullptr && iAddressLen > 0);

	return ::GetSocketLocalAddress(m_soListen, lpszAddress, iAddressLen, usPort);
}

BOOL CUdpServer::GetLocalAddress(CONNID dwConnID, TCHAR lpszAddress[], int& iAddressLen, USHORT& usPort)
{
	ASSERT(lpszAddress != nullptr && iAddressLen > 0);

	TUdpSocketObj* pSocketObj = FindSocketObj(dwConnID);

	if(TUdpSocketObj::IsValid(pSocketObj))
		return ::GetSocketLocalAddress(m_soListen, lpszAddress, iAddressLen, usPort);

	return FALSE;
}

BOOL CUdpServer::GetRemoteAddress(CONNID dwConnID, TCHAR lpszAddress[], int& iAddressLen, USHORT& usPort)
{
	ASSERT(lpszAddress != nullptr && iAddressLen > 0);

	TUdpSocketObj* pSocketObj = FindSocketObj(dwConnID);

	if(TUdpSocketObj::IsExist(pSocketObj))
	{
		ADDRESS_FAMILY usFamily;
		return ::sockaddr_IN_2_A(pSocketObj->remoteAddr, usFamily, lpszAddress, iAddressLen, usPort);
	}

	return FALSE;
}

BOOL CUdpServer::SetConnectionExtra(CONNID dwConnID, PVOID pExtra)
{
	TUdpSocketObj* pSocketObj = FindSocketObj(dwConnID);
	return SetConnectionExtra(pSocketObj, pExtra);
}

BOOL CUdpServer::SetConnectionExtra(TUdpSocketObj* pSocketObj, PVOID pExtra)
{
	if(TUdpSocketObj::IsExist(pSocketObj))
	{
		pSocketObj->extra = pExtra;
		return TRUE;
	}

	return FALSE;
}

BOOL CUdpServer::GetConnectionExtra(CONNID dwConnID, PVOID* ppExtra)
{
	TUdpSocketObj* pSocketObj = FindSocketObj(dwConnID);
	return GetConnectionExtra(pSocketObj, ppExtra);
}

BOOL CUdpServer::GetConnectionExtra(TUdpSocketObj* pSocketObj, PVOID* ppExtra)
{
	ASSERT(ppExtra != nullptr);

	if(TUdpSocketObj::IsExist(pSocketObj))
	{
		*ppExtra = pSocketObj->extra;
		return TRUE;
	}

	return FALSE;
}

BOOL CUdpServer::SetConnectionReserved(CONNID dwConnID, PVOID pReserved)
{
	TUdpSocketObj* pSocketObj = FindSocketObj(dwConnID);
	return SetConnectionReserved(pSocketObj, pReserved);
}

BOOL CUdpServer::SetConnectionReserved(TUdpSocketObj* pSocketObj, PVOID pReserved)
{
	if(TUdpSocketObj::IsExist(pSocketObj))
	{
		pSocketObj->reserved = pReserved;
		return TRUE;
	}

	return FALSE;
}

BOOL CUdpServer::GetConnectionReserved(CONNID dwConnID, PVOID* ppReserved)
{
	TUdpSocketObj* pSocketObj = FindSocketObj(dwConnID);
	return GetConnectionReserved(pSocketObj, ppReserved);
}

BOOL CUdpServer::GetConnectionReserved(TUdpSocketObj* pSocketObj, PVOID* ppReserved)
{
	ASSERT(ppReserved != nullptr);

	if(TUdpSocketObj::IsExist(pSocketObj))
	{
		*ppReserved = pSocketObj->reserved;
		return TRUE;
	}

	return FALSE;
}

BOOL CUdpServer::SetConnectionReserved2(CONNID dwConnID, PVOID pReserved2)
{
	TUdpSocketObj* pSocketObj = FindSocketObj(dwConnID);
	return SetConnectionReserved2(pSocketObj, pReserved2);
}

BOOL CUdpServer::SetConnectionReserved2(TUdpSocketObj* pSocketObj, PVOID pReserved2)
{
	if(TUdpSocketObj::IsExist(pSocketObj))
	{
		pSocketObj->reserved2 = pReserved2;
		return TRUE;
	}

	return FALSE;
}

BOOL CUdpServer::GetConnectionReserved2(CONNID dwConnID, PVOID* ppReserved2)
{
	TUdpSocketObj* pSocketObj = FindSocketObj(dwConnID);
	return GetConnectionReserved2(pSocketObj, ppReserved2);
}

BOOL CUdpServer::GetConnectionReserved2(TUdpSocketObj* pSocketObj, PVOID* ppReserved2)
{
	ASSERT(ppReserved2 != nullptr);

	if(TUdpSocketObj::IsExist(pSocketObj))
	{
		*ppReserved2 = pSocketObj->reserved2;
		return TRUE;
	}

	return FALSE;
}

BOOL CUdpServer::IsPauseReceive(CONNID dwConnID, BOOL& bPaused)
{
	::SetLastError(ERROR_CALL_NOT_IMPLEMENTED);

	bPaused = FALSE;

	return FALSE;
}

BOOL CUdpServer::IsConnected(CONNID dwConnID)
{
	TUdpSocketObj* pSocketObj = FindSocketObj(dwConnID);

	if(TUdpSocketObj::IsValid(pSocketObj))
		return pSocketObj->HasConnected();

	return FALSE;
}

BOOL CUdpServer::GetPendingDataLength(CONNID dwConnID, int& iPending)
{
	TUdpSocketObj* pSocketObj = FindSocketObj(dwConnID);

	if(TUdpSocketObj::IsValid(pSocketObj))
	{
		iPending = pSocketObj->Pending();
		return TRUE;
	}

	return FALSE;
}

DWORD CUdpServer::GetConnectionCount()
{
	return m_bfActiveSockets.Elements();
}

BOOL CUdpServer::GetAllConnectionIDs(CONNID pIDs[], DWORD& dwCount)
{
	return m_bfActiveSockets.GetAllElementIndexes(pIDs, dwCount);
}

BOOL CUdpServer::GetConnectPeriod(CONNID dwConnID, DWORD& dwPeriod)
{
	BOOL isOK					= TRUE;
	TUdpSocketObj* pSocketObj	= FindSocketObj(dwConnID);

	if(TUdpSocketObj::IsValid(pSocketObj))
		dwPeriod = ::GetTimeGap32(pSocketObj->connTime);
	else
		isOK = FALSE;

	return isOK;
}

BOOL CUdpServer::GetSilencePeriod(CONNID dwConnID, DWORD& dwPeriod)
{
	if(!m_bMarkSilence)
		return FALSE;

	BOOL isOK					= TRUE;
	TUdpSocketObj* pSocketObj	= FindSocketObj(dwConnID);

	if(TUdpSocketObj::IsValid(pSocketObj))
		dwPeriod = ::GetTimeGap32(pSocketObj->activeTime);
	else
		isOK = FALSE;

	return isOK;
}

BOOL CUdpServer::Disconnect(CONNID dwConnID, BOOL bForce)
{
	TUdpSocketObj* pSocketObj = FindSocketObj(dwConnID);

	if(!TUdpSocketObj::IsValid(pSocketObj))
	{
		::SetLastError(ERROR_OBJECT_NOT_FOUND);
		return FALSE;
	}

	return ::PostIocpDisconnect(m_hCompletePort, dwConnID);
}

BOOL CUdpServer::DisconnectLongConnections(DWORD dwPeriod, BOOL bForce)
{
	if(dwPeriod > MAX_CONNECTION_PERIOD)
		return FALSE;

	DWORD size					= 0;
	unique_ptr<CONNID[]> ids	= m_bfActiveSockets.GetAllElementIndexes(size);
	DWORD now					= ::TimeGetTime();

	for(DWORD i = 0; i < size; i++)
	{
		CONNID connID				= ids[i];
		TUdpSocketObj* pSocketObj	= FindSocketObj(connID);

		if(TUdpSocketObj::IsValid(pSocketObj) && (int)(now - pSocketObj->connTime) >= (int)dwPeriod)
			Disconnect(connID, bForce);
	}

	return TRUE;
}

BOOL CUdpServer::DisconnectSilenceConnections(DWORD dwPeriod, BOOL bForce)
{
	if(!m_bMarkSilence)
		return FALSE;
	if(dwPeriod > MAX_CONNECTION_PERIOD)
		return FALSE;

	DWORD size					= 0;
	unique_ptr<CONNID[]> ids	= m_bfActiveSockets.GetAllElementIndexes(size);
	DWORD now					= ::TimeGetTime();

	for(DWORD i = 0; i < size; i++)
	{
		CONNID connID				= ids[i];
		TUdpSocketObj* pSocketObj	= FindSocketObj(connID);

		if(TUdpSocketObj::IsValid(pSocketObj) && (int)(now - pSocketObj->activeTime) >= (int)dwPeriod)
			Disconnect(connID, bForce);
	}

	return TRUE;
}

void CUdpServer::WaitForPostReceiveRelease()
{
	while(m_iRemainPostReceives > 0)
		::WaitWithMessageLoop(50);
}

void CUdpServer::WaitForClientSocketClose()
{
	while(m_bfActiveSockets.Elements() > 0)
		::WaitWithMessageLoop(50);
}

void CUdpServer::WaitForWorkerThreadEnd()
{
	int count = (int)m_vtWorkerThreads.size();

	for(int i = 0; i < count; i++)
		::PostIocpExit(m_hCompletePort);

	int remain	= count;
	int index	= 0;

	while(remain > 0)
	{
		int wait = min(remain, MAXIMUM_WAIT_OBJECTS);
		HANDLE* pHandles = CreateLocalObjects(HANDLE, wait);

		for(int i = 0; i < wait; i++)
			pHandles[i]	= m_vtWorkerThreads[i + index];

		ENSURE(::WaitForMultipleObjects((DWORD)wait, pHandles, TRUE, INFINITE) == WAIT_OBJECT_0);

		for(int i = 0; i < wait; i++)
			::CloseHandle(pHandles[i]);

		remain	-= wait;
		index	+= wait;
	}

	m_vtWorkerThreads.clear();
}

void CUdpServer::CloseCompletePort()
{
	if(m_hCompletePort != nullptr)
	{
		::CloseHandle(m_hCompletePort);
		m_hCompletePort = nullptr;
	}
}

UINT WINAPI CUdpServer::WorkerThreadProc(LPVOID pv)
{
	CUdpServer* pServer	= (CUdpServer*)pv;
	pServer->OnWorkerThreadStart(SELF_THREAD_ID);

	while(TRUE)
	{
		DWORD dwErrorCode = NO_ERROR;
		DWORD dwBytes;
		ULONG_PTR ulCompKey;
		OVERLAPPED* pOverlapped;
		
		BOOL result = ::GetQueuedCompletionStatus
												(
													pServer->m_hCompletePort,
													&dwBytes,
													&ulCompKey,
													&pOverlapped,
													INFINITE
												);

		if(pOverlapped == nullptr)
		{
			EnIocpAction action = pServer->CheckIocpCommand(pOverlapped, dwBytes, ulCompKey);

			if(action == IOCP_ACT_CONTINUE)
				continue;
			else if(action == IOCP_ACT_BREAK)
				break;
		}

		TUdpBufferObj* pBufferObj	= CONTAINING_RECORD(pOverlapped, TUdpBufferObj, ov);
		CONNID dwConnID				= pServer->FindConnectionID(&pBufferObj->remoteAddr);

		if (!result)
		{
			DWORD dwFlag	= 0;
			DWORD dwSysCode = ::GetLastError();

			if(pServer->HasStarted())
			{
				result = ::WSAGetOverlappedResult((SOCKET)ulCompKey, &pBufferObj->ov, &dwBytes, FALSE, &dwFlag);

				if (!result)
				{
					dwErrorCode = ::WSAGetLastError();
					TRACE("GetQueuedCompletionStatus error (<S-CNNID: %Iu> SYS: %d, SOCK: %d, FLAG: %d)\n", dwConnID, dwSysCode, dwErrorCode, dwFlag);
				}
			}
			else
				dwErrorCode = dwSysCode;

			ASSERT(dwSysCode != 0 && dwErrorCode != 0);
		}

		pServer->HandleIo(dwConnID, pBufferObj, dwBytes, dwErrorCode);
	}

	pServer->OnWorkerThreadEnd(SELF_THREAD_ID);

	return 0;
}

EnIocpAction CUdpServer::CheckIocpCommand(OVERLAPPED* pOverlapped, DWORD dwBytes, ULONG_PTR ulCompKey)
{
	ASSERT(pOverlapped == nullptr);

	EnIocpAction action = IOCP_ACT_CONTINUE;

	if(dwBytes == IOCP_CMD_SEND)
		DoSend((CONNID)ulCompKey);
	else if(dwBytes == IOCP_CMD_ACCEPT)
		DoReceive(GetFreeBufferObj());
	else if(dwBytes == IOCP_CMD_DISCONNECT)
		ForceDisconnect((CONNID)ulCompKey, TRUE);
	else if(dwBytes == IOCP_CMD_TIMEOUT)
		ForceDisconnect((CONNID)ulCompKey, FALSE);
	else if(dwBytes == IOCP_CMD_EXIT && ulCompKey == 0)
		action = IOCP_ACT_BREAK;
	else
		ENSURE(FALSE);

	return action;
}

void CUdpServer::ForceDisconnect(CONNID dwConnID, BOOL bNotify)
{
	AddFreeSocketObj(dwConnID, SCF_CLOSE, SO_UNKNOWN, 0, bNotify);
}

void CUdpServer::HandleIo(CONNID dwConnID, TUdpBufferObj* pBufferObj, DWORD dwBytes, DWORD dwErrorCode)
{
	ASSERT(pBufferObj != nullptr);

	if(dwErrorCode != NO_ERROR)
	{
		HandleError(dwConnID, pBufferObj, dwErrorCode);
		return;
	}

	if(dwBytes == 0)
	{
		HandleZeroBytes(dwConnID, pBufferObj);
		return;
	}

	pBufferObj->buff.len = dwBytes;

	switch(pBufferObj->operation)
	{
	case SO_SEND:
		HandleSend(dwConnID, pBufferObj);
		break;
	case SO_RECEIVE:
		HandleReceive(dwConnID, pBufferObj);
		break;
	default:
		ASSERT(FALSE);
	}
}

void CUdpServer::HandleError(CONNID dwConnID, TUdpBufferObj* pBufferObj, DWORD dwErrorCode)
{
	if(dwConnID != 0)
		AddFreeSocketObj(dwConnID, SCF_ERROR, pBufferObj->operation, dwErrorCode);
	
	if(pBufferObj->operation == SO_RECEIVE)
		DoReceive(pBufferObj);
	else if(pBufferObj->operation != SO_SEND || pBufferObj->ReleaseSendCounter() == 0)
		AddFreeBufferObj(pBufferObj);
}

void CUdpServer::HandleZeroBytes(CONNID dwConnID, TUdpBufferObj* pBufferObj)
{
	if(pBufferObj->operation == SO_RECEIVE)
	{
		if(dwConnID == 0)
			dwConnID = HandleAccept(pBufferObj);
		
		if(dwConnID != 0)
		{
			TRACE("<S-CNNID: %Iu> recv 0 bytes (detect package)\n", dwConnID);

			TUdpSocketObj* pSocketObj = FindSocketObj(dwConnID);

			if(TUdpSocketObj::IsValid(pSocketObj))
			{
				pSocketObj->detectFails = 0;
				SendDetectPackage(dwConnID, pSocketObj);
			}			
		}

		DoReceive(pBufferObj);
	}
	else
		ENSURE(FALSE);
}

CONNID CUdpServer::HandleAccept(TUdpBufferObj* pBufferObj)
{
	CONNID dwConnID				= 0;
	TUdpSocketObj* pSocketObj	= nullptr;

	{
		CCriSecLock locallock(m_csAccept);

		dwConnID = FindConnectionID(&pBufferObj->remoteAddr);

		if(dwConnID != 0)
			return dwConnID;
		else
		{
			if(!HasStarted())
				return 0;

			if(!m_bfActiveSockets.AcquireLock(dwConnID))
			{
				::SendUdpCloseNotify(m_soListen, pBufferObj->remoteAddr);
				return 0;
			}

			pSocketObj = GetFreeSocketObj(dwConnID);
			pSocketObj->csRecv.WaitToWrite();

			AddClientSocketObj(dwConnID, pSocketObj, pBufferObj->remoteAddr);
		}
	}

	if(TriggerFireAccept(pSocketObj) == HR_ERROR)
	{
		AddFreeSocketObj(pSocketObj);
		dwConnID = 0;
	}

	pSocketObj->csRecv.WriteDone();

	return dwConnID;
}

void CUdpServer::HandleSend(CONNID dwConnID, TUdpBufferObj* pBufferObj)
{
	TUdpSocketObj* pSocketObj = FindSocketObj(dwConnID);

	if(!TUdpSocketObj::IsValid(pSocketObj))
	{
		AddFreeBufferObj(pBufferObj);
		return;
	}

	long iLength = -(long)(pBufferObj->buff.len);

	switch(m_enSendPolicy)
	{
	case SP_PACK:
		{
			::InterlockedExchangeAdd(&pSocketObj->sndCount, iLength);

			TriggerFireSend(pSocketObj, pBufferObj);

			DoSendPack(pSocketObj);
		}

		break;
	case SP_SAFE:
		{
			::InterlockedExchangeAdd(&pSocketObj->sndCount, iLength);

			TriggerFireSend(pSocketObj, pBufferObj);

			DoSendSafe(pSocketObj);
		}

		break;
	case SP_DIRECT:
		{
			::InterlockedExchangeAdd(&pSocketObj->pending, iLength);

			TriggerFireSend(pSocketObj, pBufferObj);
		}

		break;
	default:
		ASSERT(FALSE);
	}
}

void CUdpServer::HandleReceive(CONNID dwConnID, TUdpBufferObj* pBufferObj)
{
	ProcessReceive(dwConnID, pBufferObj);
	::ContinueReceiveFrom(this, pBufferObj);

	DoReceive(pBufferObj);
}

void CUdpServer::ProcessReceive(CONNID dwConnID, TUdpBufferObj* pBufferObj)
{
	if(dwConnID == 0)
		dwConnID = HandleAccept(pBufferObj);

	if(dwConnID != 0)
	{
		TUdpSocketObj* pSocketObj = FindSocketObj(dwConnID);

		if(TUdpSocketObj::IsValid(pSocketObj))
		{
			pSocketObj->detectFails = 0;
			if(m_bMarkSilence) pSocketObj->activeTime = ::TimeGetTime();

			EnHandleResult rs = TriggerFireReceive(pSocketObj, pBufferObj);

			if(rs == HR_CLOSED)
				AddFreeSocketObj(pSocketObj, SCF_CLOSE, SO_CLOSE, SE_OK, FALSE);
			else if(rs == HR_ERROR)
			{
				TRACE("<S-CNNID: %Iu> OnReceive() event return 'HR_ERROR', connection will be closed !\n", dwConnID);
				AddFreeSocketObj(pSocketObj, SCF_ERROR, SO_RECEIVE, ENSURE_ERROR_CANCELLED);
			}
		}
	}
}

void CUdpServer::ProcessReceiveBufferObj(TUdpBufferObj* pBufferObj)
{
	CONNID dwConnID = FindConnectionID(&pBufferObj->remoteAddr);
	ProcessReceive(dwConnID, pBufferObj);
}

int CUdpServer::DoReceive(TUdpBufferObj* pBufferObj)
{
	int result = NO_ERROR;

	if(!HasStarted())
		result = ERROR_INVALID_STATE;
	else
	{
		pBufferObj->buff.len = m_dwMaxDatagramSize;
		result = ::PostReceiveFrom(m_soListen, pBufferObj);
	}

	if(result != NO_ERROR)
	{
		ENSURE(!HasStarted());

		::InterlockedDecrement(&m_iRemainPostReceives);
		ASSERT(m_iRemainPostReceives >= 0);

		AddFreeBufferObj(pBufferObj);
	}

	return result;
}

BOOL CUdpServer::PauseReceive(CONNID dwConnID, BOOL bPause)
{
	::SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL CUdpServer::Send(CONNID dwConnID, const BYTE* pBuffer, int iLength, int iOffset)
{
	ASSERT(pBuffer && iLength > 0 && iLength <= (int)m_dwMaxDatagramSize);

	TUdpSocketObj* pSocketObj = FindSocketObj(dwConnID);

	return DoSend(pSocketObj, pBuffer, iLength, iOffset);
}

BOOL CUdpServer::DoSend(TUdpSocketObj* pSocketObj, const BYTE* pBuffer, int iLength, int iOffset)
{
	int result = NO_ERROR;

	if(TUdpSocketObj::IsValid(pSocketObj))
	{
		if(pBuffer && iLength > 0 && iLength <= (int)m_dwMaxDatagramSize)
		{
			if(iOffset != 0) pBuffer += iOffset;

			TUdpBufferObjPtr bufPtr(m_bfObjPool, m_bfObjPool.PickFreeItem());
			bufPtr->Cat(pBuffer, iLength);

			result = SendInternal(pSocketObj, bufPtr);
		}
		else
			result = ERROR_INVALID_PARAMETER;
	}
	else
		result = ERROR_OBJECT_NOT_FOUND;

	if(result != NO_ERROR)
		::SetLastError(result);

	return (result == NO_ERROR);
}

BOOL CUdpServer::SendPackets(CONNID dwConnID, const WSABUF pBuffers[], int iCount)
{
	ASSERT(pBuffers && iCount > 0);

	if(!pBuffers || iCount <= 0)
		return ERROR_INVALID_PARAMETER;;

	TUdpSocketObj* pSocketObj = FindSocketObj(dwConnID);

	if(!TUdpSocketObj::IsValid(pSocketObj))
	{
		::SetLastError(ERROR_OBJECT_NOT_FOUND);
		return FALSE;
	}

	int result	= NO_ERROR;
	int iLength = 0;
	int iMaxLen = (int)m_dwMaxDatagramSize;
	
	TUdpBufferObjPtr bufPtr(m_bfObjPool, m_bfObjPool.PickFreeItem());

	for(int i = 0; i < iCount; i++)
	{
		int iBufLen = pBuffers[i].len;

		if(iBufLen > 0)
		{
			BYTE* pBuffer = (BYTE*)pBuffers[i].buf;
			ASSERT(pBuffer);

			iLength += iBufLen;

			if(iLength <= iMaxLen)
				bufPtr->Cat(pBuffer, iBufLen);
			else
				break;
		}
	}

	if(iLength > 0 && iLength <= iMaxLen)
		result = SendInternal(pSocketObj, bufPtr);
	else
		result = ERROR_INCORRECT_SIZE;

	if(result != NO_ERROR)
		::SetLastError(result);

	return (result == NO_ERROR);
}

int CUdpServer::SendInternal(TUdpSocketObj* pSocketObj, TUdpBufferObjPtr& bufPtr)
{
	int result = NO_ERROR;

	{
		CCriSecLock locallock(pSocketObj->csSend);

		if(!TUdpSocketObj::IsValid(pSocketObj))
			return ERROR_OBJECT_NOT_FOUND;

		switch(m_enSendPolicy)
		{
		case SP_PACK:	result = SendPack(pSocketObj, bufPtr);		break;
		case SP_SAFE:	result = SendSafe(pSocketObj, bufPtr);		break;
		case SP_DIRECT:	result = SendDirect(pSocketObj, bufPtr);	break;
		default: ASSERT(FALSE);	result = ERROR_INVALID_INDEX;		break;
		}
	}

	return result;
}

int CUdpServer::SendPack(TUdpSocketObj* pSocketObj, TUdpBufferObjPtr& bufPtr)
{
	return CatAndPost(pSocketObj, bufPtr);
}

int CUdpServer::SendSafe(TUdpSocketObj* pSocketObj, TUdpBufferObjPtr& bufPtr)
{
	return CatAndPost(pSocketObj, bufPtr);
}

int CUdpServer::CatAndPost(TUdpSocketObj* pSocketObj, TUdpBufferObjPtr& bufPtr)
{
	int result = NO_ERROR;

	pSocketObj->pending += (int)bufPtr->buff.len;
	pSocketObj->sndBuff.PushBack(bufPtr.Detach());

	if(pSocketObj->IsCanSend() && pSocketObj->IsSmooth() && !::PostIocpSend(m_hCompletePort, pSocketObj->connID))
		result = ::GetLastError();

	return result;
}

int CUdpServer::SendDirect(TUdpSocketObj* pSocketObj, TUdpBufferObjPtr& bufPtr)
{
	TUdpBufferObj* pBufferObj	= bufPtr.Detach();
	int iLength					= (int)pBufferObj->buff.len;

	pSocketObj->remoteAddr.Copy(pBufferObj->remoteAddr);
	::InterlockedExchangeAdd(&pSocketObj->pending, iLength);

	int result		= ::PostSendTo(m_soListen, pBufferObj);
	LONG sndCounter	= pBufferObj->ReleaseSendCounter();

	if(sndCounter == 0 || result != NO_ERROR)
	{
		AddFreeBufferObj(pBufferObj);
		
		if(result != NO_ERROR)
		{
			::InterlockedExchangeAdd(&pSocketObj->pending, -iLength);

			ENSURE(!HasStarted());
		}
	}

	return result;
}

int CUdpServer::DoSend(CONNID dwConnID)
{
	TUdpSocketObj* pSocketObj = FindSocketObj(dwConnID);

	if(TUdpSocketObj::IsValid(pSocketObj))
		return DoSend(pSocketObj);

	return ERROR_OBJECT_NOT_FOUND;
}

int CUdpServer::DoSend(TUdpSocketObj* pSocketObj)
{
	switch(m_enSendPolicy)
	{
	case SP_PACK:			return DoSendPack(pSocketObj);
	case SP_SAFE:			return DoSendSafe(pSocketObj);
	default: ASSERT(FALSE);	return ERROR_INVALID_INDEX;
	}
}

int CUdpServer::DoSendPack(TUdpSocketObj* pSocketObj)
{
	if(!pSocketObj->IsCanSend())
		return NO_ERROR;

	int result = NO_ERROR;

	if(pSocketObj->IsPending() && pSocketObj->TurnOffSmooth())
	{
		{
			CCriSecLock locallock(pSocketObj->csSend);

			if(!TUdpSocketObj::IsValid(pSocketObj))
				return ERROR_OBJECT_NOT_FOUND;

			if(pSocketObj->IsPending())
				result = SendItem(pSocketObj);

			pSocketObj->TurnOnSmooth();
		}

		if(result == WSA_IO_PENDING && pSocketObj->IsSmooth())
			::PostIocpSend(m_hCompletePort, pSocketObj->connID);
	}

	if(!IOCP_SUCCESS(result))
		ENSURE(!HasStarted());

	return result;
}

int CUdpServer::DoSendSafe(TUdpSocketObj* pSocketObj)
{
	long lSendBuffSize = pSocketObj->GetSendBufferSize();

	if(pSocketObj->sndCount < lSendBuffSize && !pSocketObj->IsSmooth())
	{
		CCriSecLock locallock(pSocketObj->csSend);

		if(!TUdpSocketObj::IsValid(pSocketObj))
			return ERROR_OBJECT_NOT_FOUND;

		if(pSocketObj->sndCount < lSendBuffSize)
			pSocketObj->smooth = TRUE;
	}

	int result = NO_ERROR;

	if(pSocketObj->IsPending() && pSocketObj->IsSmooth())
	{
		CCriSecLock locallock(pSocketObj->csSend);

		if(!TUdpSocketObj::IsValid(pSocketObj))
			return ERROR_OBJECT_NOT_FOUND;

		if(pSocketObj->IsPending() && pSocketObj->IsSmooth())
		{
			pSocketObj->smooth = FALSE;

			result = SendItem(pSocketObj);

			if(result == NO_ERROR)
				pSocketObj->smooth = TRUE;
		}
	}

	if(!IOCP_SUCCESS(result))
		ENSURE(!HasStarted());

	return result;
}

int CUdpServer::SendItem(TUdpSocketObj* pSocketObj)
{
	int result = NO_ERROR;

	while(pSocketObj->sndBuff.Size() > 0)
	{
		TUdpBufferObj* pBufferObj = pSocketObj->sndBuff.PopFront();
		pSocketObj->remoteAddr.Copy(pBufferObj->remoteAddr);

		int iBufferSize		 = pBufferObj->buff.len;
		pSocketObj->pending	-= iBufferSize;

		ASSERT(iBufferSize > 0 && iBufferSize <= (int)m_dwMaxDatagramSize);

		::InterlockedExchangeAdd(&pSocketObj->sndCount, iBufferSize);

		result			= ::PostSendToNotCheck(m_soListen, pBufferObj);
		LONG sndCounter	= pBufferObj->ReleaseSendCounter();

		if(sndCounter == 0 || !IOCP_SUCCESS(result))
			AddFreeBufferObj(pBufferObj);

		if(result != NO_ERROR)
			break;
	}

	return result;
}

BOOL CUdpServer::SendDetectPackage(CONNID dwConnID, TUdpSocketObj* pSocketObj)
{
	BOOL isOK = TRUE;

	if(!HasStarted())
		isOK = FALSE;
	else
	{
		int rc = sendto(m_soListen, nullptr, 0, 0, pSocketObj->remoteAddr.Addr(), pSocketObj->remoteAddr.AddrSize());

		if(rc == SOCKET_ERROR && ::WSAGetLastError() != WSAEWOULDBLOCK)
			isOK = FALSE;

		if(isOK)
			TRACE("<S-CNNID: %Iu> send 0 bytes (detect package)\n", dwConnID);
		else
			ENSURE(!HasStarted());
	}

	return isOK;
}

void WINAPI CUdpServer::DetectConnectionProc(LPVOID pv, BOOLEAN bTimerFired)
{
	TUdpSocketObj* pSocketObj = (TUdpSocketObj*)pv;

	if(TUdpSocketObj::IsValid(pSocketObj))
	{
		CUdpServer* pServer = (CUdpServer*)pSocketObj->pHolder;

		if(pSocketObj->detectFails >= pServer->m_dwDetectAttempts)
			::PostIocpTimeout(pServer->m_hCompletePort, pSocketObj->connID);
		else
			::InterlockedIncrement(&pSocketObj->detectFails);
	}
}

#endif
