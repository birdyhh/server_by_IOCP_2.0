# server_by_IOCP_2.0

## 功能
一个基于完成端口网络服务类，使用IOCP机制的缓存池，简单的Echo服务器

## 环境要求
Windows (msvc 2013+)

## 项目执行逻辑
![一个基于WinSock服务器编程模型的IOCP网络服务](https://raw.githubusercontent.com/birdyhh/server_by_IOCP_2.0/main/process.jpg)
用法：派生一个子类，重载回调函数。这个类IOCP是本代码的核心类，用于说明WinSock服务器端编程模型中的完成端口(IOCP)的使用方法，其中的IOContext类是封装了用于每一个重叠操作的参数， 具体说明了服务器端建立完成端口、建立工作者线程、投递Recv请求、投递Accept请求的方法， 所有的客户端连入的Socket都需要绑定到IOCP上，所有从客户端发来的数据，都会调用回调函数。

## 项目启动
打开vs文件启动

## 测试
![](https://raw.githubusercontent.com/birdyhh/server_by_IOCP_2.0/main/text.png)
**测试环境**：Windows11 CPU:i5-9600k 内存：16G<br>
采用了完成端口的Server程序在10000个线程的连接下，CPU的占用非常低，反而是采用了多个并发线程的Client程序界面已经奔溃，且CPU占用高。<br>
虽然这不是服务器的压力测试，但是可以看得出Windows下IOCP的实力。
## TODO
1. 使用完成端口来传送文件<br>
2. 使用智能指针来管理资源<br>
3. 用更好的数据结构来管理数据<br>
4. 完善异常处理机制<br>
5. 添加http功能<br>

## 致谢
TCP/IP网络编程 【韩】尹圣雨 著 金国哲 译

---

Revised by Birdyhh at 2022/1
