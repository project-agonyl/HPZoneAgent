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

#include "UdpServer.h"
#include "ArqHelper.h"

#ifdef _UDP_SUPPORT

#include "Common/STLHelper.h"

class CUdpArqServer : public IArqSocket, public CUdpServer
{
	typedef CArqSessionT<CUdpArqServer, TUdpSocketObj>		CArqSession;
	typedef CArqSessionExT<CUdpArqServer, TUdpSocketObj>	CArqSessionEx;
	typedef CArqSessionPoolT<CUdpArqServer, TUdpSocketObj>	CArqSessionPool;
	typedef unordered_map<THR_ID, CBufferPtr*>				CRecvBufferMap;

	friend class											CArqSession;

public:
	virtual BOOL Send		(CONNID dwConnID, const BYTE* pBuffer, int iLength, int iOffset = 0);
	virtual BOOL SendPackets(CONNID dwConnID, const WSABUF pBuffers[], int iCount);

protected:
	virtual EnHandleResult FireAccept(TUdpSocketObj* pSocketObj);
	virtual EnHandleResult FireReceive(TUdpSocketObj* pSocketObj, const BYTE* pData, int iLength);
	virtual EnHandleResult FireClose(TUdpSocketObj* pSocketObj, EnSocketOperation enOperation, int iErrorCode);

	virtual BOOL CheckParams();
	virtual void PrepareStart();
	virtual void Reset();
	virtual void OnWorkerThreadStart(THR_ID dwThreadID);

public:
	virtual void SetNoDelay				(BOOL bNoDelay)				{ENSURE_HAS_STOPPED(); m_arqAttr.bNoDelay			= bNoDelay;}
	virtual void SetTurnoffCongestCtrl	(BOOL bTurnOff)				{ENSURE_HAS_STOPPED(); m_arqAttr.bTurnoffNc			= bTurnOff;}
	virtual void SetFlushInterval		(DWORD dwFlushInterval)		{ENSURE_HAS_STOPPED(); m_arqAttr.dwFlushInterval	= dwFlushInterval;}
	virtual void SetResendByAcks		(DWORD dwResendByAcks)		{ENSURE_HAS_STOPPED(); m_arqAttr.dwResendByAcks		= dwResendByAcks;}
	virtual void SetSendWndSize			(DWORD dwSendWndSize)		{ENSURE_HAS_STOPPED(); m_arqAttr.dwSendWndSize		= dwSendWndSize;}
	virtual void SetRecvWndSize			(DWORD dwRecvWndSize)		{ENSURE_HAS_STOPPED(); m_arqAttr.dwRecvWndSize		= dwRecvWndSize;}
	virtual void SetMinRto				(DWORD dwMinRto)			{ENSURE_HAS_STOPPED(); m_arqAttr.dwMinRto			= dwMinRto;}
	virtual void SetFastLimit			(DWORD dwFastLimit)			{ENSURE_HAS_STOPPED(); m_arqAttr.dwFastLimit		= dwFastLimit;}
	virtual void SetMaxTransUnit		(DWORD dwMaxTransUnit)		{ENSURE_HAS_STOPPED(); m_dwMtu						= dwMaxTransUnit;}
	virtual void SetMaxMessageSize		(DWORD dwMaxMessageSize)	{ENSURE_HAS_STOPPED(); m_arqAttr.dwMaxMessageSize	= dwMaxMessageSize;}
	virtual void SetHandShakeTimeout	(DWORD dwHandShakeTimeout)	{ENSURE_HAS_STOPPED(); m_arqAttr.dwHandShakeTimeout	= dwHandShakeTimeout;}

	virtual BOOL IsNoDelay				()	{return m_arqAttr.bNoDelay;}
	virtual BOOL IsTurnoffCongestCtrl	()	{return m_arqAttr.bTurnoffNc;}
	virtual DWORD GetFlushInterval		()	{return m_arqAttr.dwFlushInterval;}
	virtual DWORD GetResendByAcks		()	{return m_arqAttr.dwResendByAcks;}
	virtual DWORD GetSendWndSize		()	{return m_arqAttr.dwSendWndSize;}
	virtual DWORD GetRecvWndSize		()	{return m_dwMtu;}
	virtual DWORD GetMinRto				()	{return m_arqAttr.dwMinRto;}
	virtual DWORD GetFastLimit			()	{return m_arqAttr.dwFastLimit;}
	virtual DWORD GetMaxTransUnit		()	{return m_arqAttr.dwMtu;}
	virtual DWORD GetMaxMessageSize		()	{return m_arqAttr.dwMaxMessageSize;}
	virtual DWORD GetHandShakeTimeout	()	{return m_arqAttr.dwHandShakeTimeout;}

	virtual BOOL GetWaitingSendMessageCount	(CONNID dwConnID, int& iCount);

public:
	const TArqAttr& GetArqAttribute		()	{return m_arqAttr;}
	Fn_ArqOutputProc GetArqOutputProc	()	{return ArqOutputProc;}

private:
	int SendArq(TUdpSocketObj* pSocketObj, const BYTE* pBuffer, int iLength);

	static int ArqOutputProc(const char* pBuffer, int iLength, IKCPCB* kcp, LPVOID pv);

public:
	CUdpArqServer(IUdpServerListener* pListener)
	: CUdpServer(pListener)
	, m_dwMtu	(0)
	{
		
	}

	virtual ~CUdpArqServer()
	{
		ENSURE_STOP();
	}

private:
	static const CTimePeriod sm_tmPeriod;

private:
	DWORD			m_dwMtu;
	TArqAttr		m_arqAttr;

	CCriSec			m_csRcBuffers;
	CRecvBufferMap	m_rcBuffers;

	CArqSessionPool m_ssPool;
};

#endif
