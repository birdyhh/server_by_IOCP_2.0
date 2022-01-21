#pragma once
#include "PerIoContext.h"

//=============================================================================
//							socket结构体
// ----------------------------------------------------------------------------
// 用于绑定每一个完成端口；
// 包含与每个客户端socket有关的参数；
//=============================================================================
struct SocketContext
{
	SOCKET          b_Socket;		    //某个与服务器连接的客户端SOCKET
	SOCKADDR_IN		b_ClientAddr;       //该客户端地址
	vector<IoContext*> b_arrayIoContext;//该客户端所有IO请求的vector数组

	//构造函数
	SocketContext()                    
	{
		b_Socket = INVALID_SOCKET;
		memset(&b_ClientAddr, 0, sizeof(b_ClientAddr));
	}
	
	//析构函数
	~SocketContext()                   
	{
		if (b_Socket != INVALID_SOCKET)
		{
			closesocket(b_Socket);
			b_Socket = INVALID_SOCKET;
		}
		for (size_t i = 0; i < b_arrayIoContext.size(); i++)
		{
			delete b_arrayIoContext.at(i);
		}
		b_arrayIoContext.clear();
	}

	//进行套接字操作时，调用此函数返回IOContext结构
	IoContext* GetNewIoContext()       
	{
		IoContext* p = new IoContext;
		b_arrayIoContext.emplace_back(p);
		return p;
	}

	//删除指定的IoContext
	void RemoveContext(IoContext* pContext)
	{
		if (pContext == nullptr)
		{
			return;
		}
		vector<IoContext*>::iterator it;
		it = b_arrayIoContext.begin();
		while (it != b_arrayIoContext.end())
		{
			IoContext* pOjb = *it;
			if (pContext == pOjb)
			{
				delete pContext;
				pContext = nullptr;
				it = b_arrayIoContext.erase(it);
				break;
			}
			it++;
		}
	}
};
