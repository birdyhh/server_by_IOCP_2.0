#pragma once
#include <winsock2.h>
#include <MSWSock.h>
#include <winnt.h>
#include <vector>
#include <string>

using namespace std;

#define MAX_BUFFER_LEN (1024*8)        //8K缓存空间
//=============================================================================
//                       PostType枚举类
// ----------------------------------------------------------------------------
//定义在完成端口上投递的I/O操作类型
//=============================================================================
enum class PostType
{
	INIT,                              //初始化时使用
	ACCEPT,                            //标志投递的是ACCEPT操作
	SEND,                              //标志投递的是SEND操作
	RECV                               //标志投递的是RECV操作
};


//=============================================================================
//			      IoContext数据结构体（含每次重叠IO操作的参数）
// ----------------------------------------------------------------------------
// 每次重叠IO操作所需的数据结构：
// OVERLAPPED结构、关联的套接字、缓存去、IO操作类型
//=============================================================================
struct IoContext
{
	OVERLAPPED b_Overlapped;           //使用重叠IO每次都需要
	PostType   b_PostType;             //标识每次网络操作的PostType类型
	SOCKET     b_acceptSocket;         //这一次网络操作所使用的套接字
	WSABUF     b_wsaBuf;               //WSA缓存，作为重叠操作中传递的参数
	char       b_szBuffer[MAX_BUFFER_LEN];//WSABUF中的字符缓冲区
	DWORD      b_nTotalBytes;          //数据的总字节数
	DWORD      b_nSentBytes;           //已经发送的字节数，未发送为0

	IoContext()                        //构造函数，初始化结构内的值
	{
		b_PostType = PostType::INIT;
		ZeroMemory(&b_Overlapped, sizeof(b_Overlapped));
		ZeroMemory(&b_szBuffer, MAX_BUFFER_LEN);
		b_acceptSocket = INVALID_SOCKET;
		b_wsaBuf.buf = b_szBuffer;
		b_wsaBuf.len = MAX_BUFFER_LEN;
		b_nSentBytes = 0;
		b_nTotalBytes = 0;
	}

	~IoContext()
	{
		if (b_acceptSocket == INVALID_SOCKET)
		{
			//析构前已关闭socket
			b_acceptSocket = INVALID_SOCKET;
		}
	}

	void ResetBuffer()                 //重置缓冲区
	{
		ZeroMemory(b_szBuffer, MAX_BUFFER_LEN);
		b_wsaBuf.len = MAX_BUFFER_LEN;
		b_wsaBuf.buf = b_szBuffer;
	}
};