//�������̨Ӧ�ó������ڵ㡣
#include "stdafx.h"
#include "IocpModel.h"

void print_time()
{
	SYSTEMTIME sysTime = { 0 };
	GetLocalTime(&sysTime);
	printf("%4d-%02d-%02d %02d:%02d:%02d.%03d��",
		sysTime.wYear, sysTime.wMonth, sysTime.wDay,
		sysTime.wHour, sysTime.wMinute, sysTime.wSecond,
		sysTime.wMilliseconds);
}

class CMyServer : public CIocpModel
{
	CRITICAL_SECTION m_csLog; // ����Worker�߳�ͬ���Ļ�����

public:
	CMyServer()
	{
		InitializeCriticalSection(&m_csLog);
	}
	~CMyServer()
	{
		DeleteCriticalSection(&m_csLog);
	}

	//��־��ӡ
	void _ShowMessage(const char* szFormat, ...)
	{
		//printf(".");
		//return;
		__try
		{
			EnterCriticalSection(&m_csLog);
			print_time();
			// ����䳤����
			va_list arglist;
			va_start(arglist, szFormat);
			vprintf(szFormat, arglist);
			va_end(arglist);
			printf("\n");
			return;
		}
		__finally
		{
			::LeaveCriticalSection(&m_csLog);
		}
	}
	// ������
	void OnConnectionAccepted(SocketContext* pSoContext)
	{
		_ShowMessage("A connection is accepted��Current connections��%d",
			GetConnectCount());
	}

	// ���ӹر�
	void OnConnectionClosed(SocketContext* pSoContext)
	{
		_ShowMessage("A connection is closed��Current connections��%d",
			GetConnectCount());
	}

	// �����Ϸ�������
	void OnConnectionError(SocketContext* pSoContext, int error)
	{
		_ShowMessage("A connection have an error��%d��Current connections��%d",
			error, GetConnectCount());
	}

	// ���������
	void OnRecvCompleted(SocketContext* pSoContext, IoContext* pIoContext)
	{
		//_ShowMessage("Recv data��%s", pIoContext->m_wsaBuf.buf);
		//SendData(pSoContext, pIoContext->m_wsaBuf.buf, pIoContext->m_wsaBuf.len);
		SendData(pSoContext, pIoContext); // ����������ɣ�ԭ�ⲻ������ȥ
	}

	// д�������
	void OnSendCompleted(SocketContext* pSoContext, IoContext* pIoContext)
	{
		//_ShowMessage("Send data successful��");
		// ����������ɣ���ʼ��������
		RecvData(pSoContext, pIoContext);
	}
};

int main()
{
	CMyServer* pServer = new CMyServer;
	pServer->_ShowMessage("Current Process Id=%d", GetCurrentProcessId());
	pServer->_ShowMessage("Current Thread Id=%d", GetCurrentThreadId());
	// ��������
	if (pServer->Start())
	{
		pServer->_ShowMessage("Server start successful on port:%d\n",
			pServer->GetPort());
	}
	else
	{
		pServer->_ShowMessage("Server start failed!");
		return 0;
	}

	// �����¼�������ServerShutdown�����ܹ��ر��Լ�
	HANDLE hEvent = ::CreateEvent(NULL, FALSE, FALSE, L"ShutdownEvent");
	if (hEvent != NULL)
	{
		//�����ȴ��ر��ź�
		::WaitForSingleObject(hEvent, INFINITE);
		::CloseHandle(hEvent);
	}
	// �رշ���
	pServer->Stop();
	delete pServer;

	print_time();
	printf("Server is closed!!!");
	return 0;
}
