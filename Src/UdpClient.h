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
 
#pragma once

#include "SocketHelper.h"

#ifdef _UDP_SUPPORT

class CUdpClient : public IUdpClient
{
public:
	virtual BOOL Start	(LPCTSTR lpszRemoteAddress, USHORT usPort, BOOL bAsyncConnect = TRUE, LPCTSTR lpszBindAddress = nullptr, USHORT usLocalPort = 0);
	virtual BOOL Stop	();
	virtual BOOL Send	(const BYTE* pBuffer, int iLength, int iOffset = 0)	{return DoSend(pBuffer, iLength, iOffset);}
	virtual BOOL SendPackets	(const WSABUF pBuffers[], int iCount);
	virtual BOOL PauseReceive	(BOOL bPause = TRUE);
	virtual BOOL Wait			(DWORD dwMilliseconds = INFINITE) {return m_evWait.Wait(dwMilliseconds);}
	virtual BOOL			HasStarted			()	{return m_enState == SS_STARTED || m_enState == SS_STARTING;}
	virtual EnServiceState	GetState			()	{return m_enState;}
	virtual CONNID			GetConnectionID		()	{return m_dwConnID;}
	virtual EnSocketError	GetLastError		()	{return m_enLastError;}
	virtual LPCTSTR			GetLastErrorDesc	()	{return ::GetSocketErrorDesc(m_enLastError);}

	virtual BOOL GetLocalAddress		(TCHAR lpszAddress[], int& iAddressLen, USHORT& usPort);
	virtual BOOL GetRemoteHost			(TCHAR lpszHost[], int& iHostLen, USHORT& usPort);
	virtual BOOL GetPendingDataLength	(int& iPending) {iPending = m_iPending; return HasStarted();}
	virtual BOOL IsPauseReceive			(BOOL& bPaused) {bPaused = m_bPaused; return HasStarted();}
	virtual BOOL IsConnected			()				{return m_bConnected;}

public:
	virtual BOOL IsSecure				() {return FALSE;}

	virtual void SetReuseAddressPolicy	(EnReuseAddressPolicy enReusePolicy){ENSURE_HAS_STOPPED(); m_enReusePolicy			= enReusePolicy;}
	virtual void SetMaxDatagramSize		(DWORD dwMaxDatagramSize)			{ENSURE_HAS_STOPPED(); m_dwMaxDatagramSize		= dwMaxDatagramSize;}
	virtual void SetDetectAttempts		(DWORD dwDetectAttempts)			{ENSURE_HAS_STOPPED(); m_dwDetectAttempts		= dwDetectAttempts;}
	virtual void SetDetectInterval		(DWORD dwDetectInterval)			{ENSURE_HAS_STOPPED(); m_dwDetectInterval		= dwDetectInterval;}
	virtual void SetFreeBufferPoolSize	(DWORD dwFreeBufferPoolSize)		{ENSURE_HAS_STOPPED(); m_dwFreeBufferPoolSize	= dwFreeBufferPoolSize;}
	virtual void SetFreeBufferPoolHold	(DWORD dwFreeBufferPoolHold)		{ENSURE_HAS_STOPPED(); m_dwFreeBufferPoolHold	= dwFreeBufferPoolHold;}
	virtual void SetExtra				(PVOID pExtra)						{m_pExtra										= pExtra;}						


	virtual EnReuseAddressPolicy GetReuseAddressPolicy	()	{return m_enReusePolicy;}
	virtual DWORD GetMaxDatagramSize	()	{return m_dwMaxDatagramSize;}
	virtual DWORD GetDetectAttempts		()	{return m_dwDetectAttempts;}
	virtual DWORD GetDetectInterval		()	{return m_dwDetectInterval;}
	virtual DWORD GetFreeBufferPoolSize	()	{return m_dwFreeBufferPoolSize;}
	virtual DWORD GetFreeBufferPoolHold	()	{return m_dwFreeBufferPoolHold;}
	virtual PVOID GetExtra				()	{return m_pExtra;}

protected:
	virtual EnHandleResult FirePrepareConnect(SOCKET socket)
		{return DoFirePrepareConnect(this, socket);}
	virtual EnHandleResult FireConnect()
		{
			EnHandleResult rs		= DoFireConnect(this);
			if(rs != HR_ERROR) rs	= FireHandShake();
			return rs;
		}
	virtual EnHandleResult FireHandShake()
		{return DoFireHandShake(this);}
	virtual EnHandleResult FireSend(const BYTE* pData, int iLength)
		{return DoFireSend(this, pData, iLength);}
	virtual EnHandleResult FireReceive(const BYTE* pData, int iLength)
		{return DoFireReceive(this, pData, iLength);}
	virtual EnHandleResult FireReceive(int iLength)
		{return DoFireReceive(this, iLength);}
	virtual EnHandleResult FireClose(EnSocketOperation enOperation, int iErrorCode)
		{return DoFireClose(this, enOperation, iErrorCode);}

	virtual EnHandleResult DoFirePrepareConnect(IUdpClient* pSender, SOCKET socket)
		{return m_pListener->OnPrepareConnect(pSender, pSender->GetConnectionID(), socket);}
	virtual EnHandleResult DoFireConnect(IUdpClient* pSender)
		{return m_pListener->OnConnect(pSender, pSender->GetConnectionID());}
	virtual EnHandleResult DoFireHandShake(IUdpClient* pSender)
		{return m_pListener->OnHandShake(pSender, pSender->GetConnectionID());}
	virtual EnHandleResult DoFireSend(IUdpClient* pSender, const BYTE* pData, int iLength)
		{return m_pListener->OnSend(pSender, pSender->GetConnectionID(), pData, iLength);}
	virtual EnHandleResult DoFireReceive(IUdpClient* pSender, const BYTE* pData, int iLength)
		{return m_pListener->OnReceive(pSender, pSender->GetConnectionID(), pData, iLength);}
	virtual EnHandleResult DoFireReceive(IUdpClient* pSender, int iLength)
		{return m_pListener->OnReceive(pSender, pSender->GetConnectionID(), iLength);}
	virtual EnHandleResult DoFireClose(IUdpClient* pSender, EnSocketOperation enOperation, int iErrorCode)
		{return m_pListener->OnClose(pSender, pSender->GetConnectionID(), enOperation, iErrorCode);}

	void SetLastError(EnSocketError code, LPCSTR func, int ec);
	virtual BOOL CheckParams();
	virtual void PrepareStart();
	virtual void Reset();

	virtual void OnWorkerThreadStart(THR_ID dwThreadID) {}
	virtual void OnWorkerThreadEnd(THR_ID dwThreadID) {}

	virtual HANDLE GetUserEvent() {return nullptr;}
	virtual BOOL OnUserEvent() {return TRUE;}

	static BOOL DoSend(CUdpClient* pClient, const BYTE* pBuffer, int iLength, int iOffset = 0)
		{return pClient->DoSend(pBuffer, iLength, iOffset);}

protected:
	void SetReserved	(PVOID pReserved)	{m_pReserved = pReserved;}						
	PVOID GetReserved	()					{return m_pReserved;}
	BOOL GetRemoteHost	(LPCSTR* lpszHost, USHORT* pusPort = nullptr);

private:
	BOOL DoSend			(const BYTE* pBuffer, int iLength, int iOffset = 0);

	void SetRemoteHost	(LPCTSTR lpszHost, USHORT usPort);
	void SetConnected	(BOOL bConnected = TRUE) {m_bConnected = bConnected; if(bConnected) m_enState = SS_STARTED;}

	BOOL CheckStarting();
	BOOL CheckStoping(DWORD dwCurrentThreadID);
	BOOL CreateClientSocket(LPCTSTR lpszRemoteAddress, HP_SOCKADDR& addrRemote, USHORT usPort, LPCTSTR lpszBindAddress, HP_SOCKADDR& addrBind);
	BOOL BindClientSocket(const HP_SOCKADDR& addrBind, const HP_SOCKADDR& addrRemote, USHORT usLocalPort);
	BOOL ConnectToServer(const HP_SOCKADDR& addrRemote, BOOL bAsyncConnect);
	BOOL CreateWorkerThread();
	BOOL ProcessNetworkEvent();
	BOOL ReadData();
	BOOL SendData();
	TItem* GetSendBuffer();
	int SendInternal(TItemPtr& itPtr);
	void WaitForWorkerThreadEnd(DWORD dwCurrentThreadID);
	void CheckConnected();

	BOOL HandleError(WSANETWORKEVENTS& events);
	BOOL HandleRead(WSANETWORKEVENTS& events);
	BOOL HandleWrite(WSANETWORKEVENTS& events);
	BOOL HandleConnect(WSANETWORKEVENTS& events);
	BOOL HandleClose(WSANETWORKEVENTS& events);

	BOOL CheckConnection();
	int DetectConnection();
	BOOL IsNeedDetect	() {return m_dwDetectAttempts > 0 && m_dwDetectInterval > 0;}

	static UINT WINAPI WorkerThreadProc(LPVOID pv);

public:
	CUdpClient(IUdpClientListener* pListener)
	: m_pListener			(pListener)
	, m_lsSend				(m_itPool)
	, m_soClient			(INVALID_SOCKET)
	, m_evSocket			(nullptr)
	, m_dwConnID			(0)
	, m_usPort				(0)
	, m_hWorker				(nullptr)
	, m_dwWorkerID			(0)
	, m_bPaused				(FALSE)
	, m_iPending			(0)
	, m_bConnected			(FALSE)
	, m_enLastError			(SE_OK)
	, m_enState				(SS_STOPPED)
	, m_dwDetectFails		(0)
	, m_pExtra				(nullptr)
	, m_pReserved			(nullptr)
	, m_enReusePolicy		(RAP_ADDR_ONLY)
	, m_dwMaxDatagramSize	(DEFAULT_UDP_MAX_DATAGRAM_SIZE)
	, m_dwFreeBufferPoolSize(DEFAULT_CLIENT_FREE_BUFFER_POOL_SIZE)
	, m_dwFreeBufferPoolHold(DEFAULT_CLIENT_FREE_BUFFER_POOL_HOLD)
	, m_dwDetectAttempts	(DEFAULT_UDP_DETECT_ATTEMPTS)
	, m_dwDetectInterval	(DEFAULT_UDP_DETECT_INTERVAL)
	, m_evWait				(TRUE, TRUE)
	{
		ASSERT(sm_wsSocket.IsValid());
		ASSERT(m_pListener);
	}

	virtual ~CUdpClient()
	{
		ENSURE_STOP();
	}

private:
	static const CInitSocket sm_wsSocket;

private:
	CEvt				m_evWait;

	IUdpClientListener*	m_pListener;
	TClientCloseContext m_ccContext;

	SOCKET				m_soClient;
	HANDLE				m_evSocket;
	CONNID				m_dwConnID;

	EnReuseAddressPolicy m_enReusePolicy;
	DWORD				m_dwMaxDatagramSize;
	DWORD				m_dwFreeBufferPoolSize;
	DWORD				m_dwFreeBufferPoolHold;
	DWORD				m_dwDetectAttempts;
	DWORD				m_dwDetectInterval;

	HANDLE				m_hWorker;
	UINT				m_dwWorkerID;

	EnSocketError		m_enLastError;
	volatile BOOL		m_bConnected;
	volatile EnServiceState	m_enState;

	PVOID				m_pExtra;
	PVOID				m_pReserved;

	CBufferPtr			m_rcBuffer;

protected:
	CStringA			m_strHost;
	USHORT				m_usPort;

	CItemPool			m_itPool;

private:
	CSpinGuard			m_csState;

	CCriSec				m_csSend;
	TItemList			m_lsSend;

	CEvt				m_evBuffer;
	CEvt				m_evWorker;
	CEvt				m_evUnpause;

	volatile int		m_iPending;
	volatile BOOL		m_bPaused;
	volatile DWORD		m_dwDetectFails;
};

#endif
