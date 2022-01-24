//定义控制台应用程序的入口点。
#include "stdafx.h"
#include "IocpModel.h"

void print_time()
{
	SYSTEMTIME sysTime = { 0 };
	GetLocalTime(&sysTime);
	printf("%4d-%02d-%02d %02d:%02d:%02d.%03d：",
		sysTime.wYear, sysTime.wMonth, sysTime.wDay,
		sysTime.wHour, sysTime.wMinute, sysTime.wSecond,
		sysTime.wMilliseconds);
}

class CMyServer : public CIocpModel
{
	CRITICAL_SECTION m_csLog; // 用于Worker线程同步的互斥量

public:
	CMyServer()
	{
		InitializeCriticalSection(&m_csLog);
	}
	~CMyServer()
	{
		DeleteCriticalSection(&m_csLog);
	}

	//日志打印
	void _ShowMessage(const char* szFormat, ...)
	{
		//printf(".");
		//return;
		__try
		{
			EnterCriticalSection(&m_csLog);
			print_time();
			// 处理变长参数
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
	// 新连接
	void OnConnectionAccepted(SocketContext* pSoContext)
	{
		_ShowMessage("A connection is accepted，Current connections：%d",
			GetConnectCount());
	}

	// 连接关闭
	void OnConnectionClosed(SocketContext* pSoContext)
	{
		_ShowMessage("A connection is closed，Current connections：%d",
			GetConnectCount());
	}

	// 连接上发生错误
	void OnConnectionError(SocketContext* pSoContext, int error)
	{
		_ShowMessage("A connection have an error：%d，Current connections：%d",
			error, GetConnectCount());
	}

	// 读操作完成
	void OnRecvCompleted(SocketContext* pSoContext, IoContext* pIoContext)
	{
		//_ShowMessage("Recv data：%s", pIoContext->m_wsaBuf.buf);
		//SendData(pSoContext, pIoContext->m_wsaBuf.buf, pIoContext->m_wsaBuf.len);
		SendData(pSoContext, pIoContext); // 接收数据完成，原封不动发回去
	}

	// 写操作完成
	void OnSendCompleted(SocketContext* pSoContext, IoContext* pIoContext)
	{
		//_ShowMessage("Send data successful！");
		// 发送数据完成，开始接收数据
		RecvData(pSoContext, pIoContext);
	}
};

int main()
{
	CMyServer* pServer = new CMyServer;
	pServer->_ShowMessage("Current Process Id=%d", GetCurrentProcessId());
	pServer->_ShowMessage("Current Thread Id=%d", GetCurrentThreadId());
	// 开启服务
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

	// 创建事件对象，让ServerShutdown程序能够关闭自己
	HANDLE hEvent = ::CreateEvent(NULL, FALSE, FALSE, L"ShutdownEvent");
	if (hEvent != NULL)
	{
		//阻塞等待关闭信号
		::WaitForSingleObject(hEvent, INFINITE);
		::CloseHandle(hEvent);
	}
	// 关闭服务
	pServer->Stop();
	delete pServer;

	print_time();
	printf("Server is closed!!!");
	return 0;
}
