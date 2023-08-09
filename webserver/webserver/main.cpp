#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<errno.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include<signal.h>
#include"locker.h"
#include"threadpool.h"
#include"http_conn.h"

#define MAX_FD 65535//最大文件描述符个数
#define MAX_EVENT_NUMBER 10000 //监听的最大事件数量

//添加信号捕捉
//handler 是一个函数指针类型，它表示传入的参数是一个指向函数的指针。
void addsig(int sig, void(handler)(int)) {
	//sigaction 是一个用于信号处理的函数。
	struct sigaction sa;
	memset(&sa, '\0',sizeof(sa));// 清空 sa 结构体
	sa.sa_handler = handler;// 设置信号处理函数
	// 在 sa.sa_mask 中将所有信号都添加到信号屏蔽字中，
	//以防止在处理当前信号时，其他信号的干扰。
	sigfillset(&sa.sa_mask);
	// 调用 sigaction 函数来注册信号处理函数
	sigaction(sig,&sa,NULL);
	
}

//添加文件描述符到epoll中
//extern: extern: 这是一个C++ 关键字，用于表示函数的定义在其他地方，
//并且需要在当前文件中进行声明以便使用。
extern void addfd(int epollfd, int fd, bool one_shot);
//从epoll中删除文件描述符
extern void removefd(int epollfd, int fd);
//修改文件描述符
extern void modfd(int epollfd, int fd, int ev);



int main(int argc, char* argv[]) {

	if (argc <= 1) {
		//%s 是字符串的格式占位符，用于输出一个字符串，它会被
		//basename(argv[0]) 替换，显示了程序的名称（不包含路径）。
		// basename(argv[0])，这是一个标准库函数，用于从完整路径中提取文件名部分。
		//它需要包含头文件 <libgen.h>。
		printf("按照如下格式运行: %s port_number\n", basename(argv[0]));
		exit(-1);
	}
	//获取端口号 atoi 函数接受一个指向字符串的指针 str，并尝试将该字符串转换为整数。
	int port = atoi(argv[1]);

	//对SIGPIE信号进行处理
	//SIGPIPE 是一个在 UNIX 系统中的信号常量，表示当向一个已经关闭的写端的
	//管道或套接字写入数据时，会产生该信号。

	//SIG_IGN 是另一个信号常量，表示忽略接收到的信号。
	addsig(SIGPIPE,SIG_IGN);

	//创建线程池，初始化线程池
	threadpool<http_conn>* pool = NULL;
	try {
		pool = new threadpool<http_conn>;
	}
	catch (...) {
		exit(-1);
	}

	//创建数组用于保存所有的客户端信息
	http_conn* users = new http_conn[MAX_FD];

	//创建监听的套接字
	int listenfd = socket(PF_INET, SOCK_STREAM, 0);

	//设置端口复用,设定在绑定之前
	int reuse = 1;
	setsockopt(listenfd,SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	//绑定
	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(port);
	bind(listenfd, (struct sockaddr*)&address, sizeof(address));

	//监听
	listen(listenfd, 5);

	//创建epoll对象，事件数组，添加
	epoll_event events[MAX_EVENT_NUMBER];
	int epollfd = epoll_create(5);

	//将监听的文件描述符添加到epoll对象中
	addfd(epollfd, listenfd, false);
	//(在http_conn头文件中添加静态属性)
	http_conn::m_epollfd = epollfd;

	while (true) {
		int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
		if ((num < 0) && (errno != EINTR)) {//调用epoll失败
			printf("epoll failure\n");
			break;
		}

		//循环遍历事件数组
		for (int i = 0; i < num; i++) {
			int sockfd = events[i].data.fd;
			if (sockfd == listenfd) {
				//有客户端连接进来
				struct sockaddr_in client_address;
				socklen_t client_addrlen = sizeof(client_address);
				int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlen);
				//缺少判断
				if (connfd < 0) {
					printf("errno is: %d\n", errno);
					continue;
				}
				if (http_conn::m_user_count >= MAX_FD) {
					//目前连接数满了
					//给客户端洗了一个信息：服务器正忙。
					close(connfd);
					continue;
				}
				//将新的客户的数据初始化，放到数组中
				users[connfd].init(connfd, client_address);
				//(在http_conn中写init函数)
			}
			else if (events[i].events &(EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
				//对方异常断开或者错误等事件。
				//(关闭连接函数close_conn())
				users[sockfd].close_conn();
			}
			else if (events[i].events & EPOLLIN) {//按位运算，非零值发生可读事件。
				if (users[sockfd].read()) {
					//一次性把所有数据都读完
					pool->append(users + sockfd);
				}else {
					users[sockfd].close_conn();
				}
			}
			else if (events[i].events & EPOLLOUT) {
				if ( !users[sockfd].write()) {//一次性写完所有数据
					users[sockfd].close_conn();
				}
			}
		}

	}
	close(epollfd);
	close(listenfd);
	delete[] users;
	delete pool;
	return 0;
}