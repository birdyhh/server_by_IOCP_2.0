#pragma once
#include "PerSocketContext.h"

#define WORKER_THREADS_PER_PROCESS 2             //每个处理器上有多少个线程
#define MAX_LISTEN_SOCKET          SOMAXCONN     //同时监听的SOCKET数量
#define MAX_POST_ACCEPT            10			 //同时投递的AcceptEx请求的数量
#define EXIT_CODE                  NULL			 //传递给Worker线程的推出信号
#define DEFAULT_IP                 "127.0.0.1"   //默认IP地址
#define DEFAULT_PORT               10240         //默认端口号

#define RELEASE_ARRAY(x) {if(x != nullptr){delete []x; x = nullptr;}}
#define RELEASE_POINTER(x) {if(x != nullptr){delete x; x = nullptr;}}
#define RELEASE_HANDLE(x) {if(x != nullptr && x != INVALID_HANDLE_VALUE)\
	{ CloseHandle(x);x=nullptr;}}				 //释放句柄宏
#define RELEASE_SOCKET(x) {if(x != NULL && x != INVALID_SOCKET)\
	{ closesocket(x);x=INVALID_SOCKET;}}          //释放Socket宏

//=============================================================================
//						CIocpModel类定义
//=============================================================================
typedef void (*LOG_FUNC)(const string& strInfo);

class CIocpModel;
//工作者线程的线程参数
struct WorkerThreadParam
{
	CIocpModel* pIocpModel;                      //类指针
	int nThreadNo;                               //线程编号
	int nThreadId;								 //线程ID
};

class CIocpModel
{
private:
	HANDLE                 b_hShutdownEvent;     //用来通知线程，为了更好的退出
	HANDLE                 b_hIOCompletionPort;  //完成端口的句柄
	HANDLE*                b_phWorkerThreads;    //工作者线程的句柄指针
	int                    b_nThreads;           //生成的线程数量
	string                 b_strIP;              //服务器端的IP地址
	int                    b_nPorts;             //服务器端的监听地址
	CRITICAL_SECTION       b_csContextList;      //用于Worker线程同步的互斥量
	vector<SocketContext*> b_arrayClientContext;//客户端Socket的Context信息
	SocketContext*         b_pListenContext;     //用于监听Socket的Context信息
	LONG                   acceptPostCount;      //当前投递的Accept数量
	LONG                   connectCount;		 //当前的连接数量
	LONG                   errorCount;			 //当前的错误数量

	//GetAcceptExSockAddrs函数指针
	LPFN_GETACCEPTEXSOCKADDRS b_lpfnGetAcceptExSockAddrs;
	//Accept函数指针
	LPFN_ACCEPTEX b_lpfnAcceptEx;

public:
	CIocpModel();
	~CIocpModel();

	//加载Socket库
	bool LoadSocketLib();
	//卸载Socket库
	void UnloadSocketLib() noexcept
	{
		WSACleanup();
	}
	//启动服务器
	bool Start(int port = DEFAULT_PORT);
	//停止服务器
	void Stop();
	//获得本机的IP地址
	string GetLocalIP();
	// 向指定客户端发送数据
	bool SendData(SocketContext* pSoContext, char* data, int size);
	bool SendData(SocketContext* pSoContext, IoContext* pIoContext);
	// 继续接收指定客户端的数据
	bool RecvData(SocketContext* pSoContext, IoContext* pIoContext);

	// 获取当前连接数
	int GetConnectCount() { return connectCount; }
	// 获取当前监听端口
	unsigned int GetPort() { return b_nPorts; }

	// 事件通知函数(派生类重载此族函数)
	virtual void OnConnectionAccepted(SocketContext* pSoContext) {};
	virtual void OnConnectionClosed(SocketContext* pSoContext) {};
	virtual void OnConnectionError(SocketContext* pSoContext, int error) {};
	virtual void OnRecvCompleted(SocketContext* pSoContext, IoContext* pIoContext)
	{
		SendData(pSoContext, pIoContext); // 接收数据完成，原封不动发回去
	};
	virtual void OnSendCompleted(SocketContext* pSoContext, IoContext* pIoContext)
	{
		RecvData(pSoContext, pIoContext); // 发送数据完成，继续接收数据
	};

protected:
	//初始化IOCP
	bool _InitializeIOCP();
	//初始化Socket
	bool _InitializeListenSocket();
	//最后释放资源
	void _DeInitialize();
	//投递AccpetEx请求
	bool _PostAccept(IoContext* pIoContext);
	//有客户端连接时，进行处理
	bool _DoAccept(SocketContext* pSoContext, IoContext* pIoContext);
	//投递WSARecv用于接收数据
	bool _PostRecv(SocketContext* pSoContext, IoContext* pIoContext);
	//数据到达，数组存放在pIoContext参数中
	bool _DoRecv(SocketContext* pSoContext, IoContext* pIoContext);
	//投递WSASend，用于发送数据
	bool _PostSend(SocketContext* pSoContext, IoContext* pIoContext);
	bool _DoSend(SocketContext* pSoContext, IoContext* pIoContext);
	bool _DoClose(SocketContext* pSoContext);
	//将客户端socket的相关信息存储到数组中
	void _AddToContextList(SocketContext* pSoContext);
	//将客户端socket的信息从数组中移除
	void _RemoveContext(SocketContext* pSoContext);
	// 清空客户端socket信息
	void _ClearContextList();
	// 将句柄绑定到完成端口中
	bool _AssociateWithIOCP(SocketContext* pSoContext);
	// 处理完成端口上的错误
	bool HandleError(SocketContext* pSoContext, const DWORD& dwErr);
	//获得本机的处理器数量
	int _GetNumOfProcessors() noexcept;
	//判断客户端Socket是否已经断开
	bool _IsSocketAlive(SOCKET s) noexcept;

	//线程函数，为IOCP请求服务的工作者线程
	//须声明为静态全局函数，否则无法被调用
	static DWORD WINAPI _WorkerThread(LPVOID lpParam);

	//在主页面输出信息
	virtual void _ShowMessage(const char* szFormat, ...);

public:
	void SetLogFunc(LOG_FUNC fn) { b_LogFunc = fn; }
protected:
	LOG_FUNC b_LogFunc;
};

