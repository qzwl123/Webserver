/*************************************************************************
	> File Name: main.cpp
	> Author: 秃头王
	> Mail: 1658339000@qq.com
	> Created Time: 2022年07月08日 星期五 07时24分18秒
 ************************************************************************/

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include "locker.h"
#include <signal.h>
#include <list>
#include "threadpool.h"
#include "http_conn.h"

// 最大的文件描述符个数
#define MAX_FD 65535

// 监听的最大事件数量
#define MAX_EVENT_NUMBER 10000

using namespace std;

// 添加信号捕捉
void addsig(int sig, void(handler)(int)) {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	sigfillset(&sa.sa_mask);
	sigaction(sig, &sa, nullptr);
	return ;
}

// 添加文件描述符到epoll中
extern void addfd(int epollfd, int fd, bool one_shot);
// 从epoll中删除文件描述符
extern void removefd(int epolld, int fd);
// 修改文件描述符
extern void modfd(int epolld, int fd);

int main(int argc, char *argv[]) {

	if(argc <= 1) {
		printf("按照如下格式运行: %s port_number\n", basename(argv[0]));
		exit(-1);
	}

	// 获取端口号
	int port = atoi(argv[1]);

	// 对SIGPIE信号进行处理
	addsig(SIGPIPE, SIG_IGN);

	// 创建线程池、初始化线程池
	threadpool<http_conn> *pool = NULL;
	try {
		pool = new  threadpool<http_conn>;
	} catch(...) {
		exit(-1);
	}
 

	// 创建一个数组用于保存所有的客户端信息
	http_conn *users = new http_conn[MAX_FD];

	// 创建监听套接字socket
	int listenfd = socket(PF_INET, SOCK_STREAM, 0);

	if(listenfd == -1) {
		perror("socket");
		exit(-1);
	}

	// 设置端口复用
	int reuse = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	// 绑定
	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(port);
	int ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
	if(ret == -1) {
		perror("bind");
		exit(-1);
	}

	// 监听
	ret = listen(listenfd, 5);
	if(ret == -1) {
		perror("listen");
		exit(-1);
	}

	// 创建epoll对象, 事件数组,添加
	epoll_event events[MAX_EVENT_NUMBER];
	int epollfd = epoll_create(5);

	// 将监听的文件描述符添加到epoll对象中
	addfd(epollfd, listenfd, false);
	http_conn::m_epollfd = epollfd;

	while(true) {
		int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
		if(num < 0 && (errno != EINTR)) {
			printf("epoll failure\n");
			break;
		}

		// 循环遍历
		for(int i = 0; i < num; ++i) {
			int sockfd = events[i].data.fd;
			if(sockfd == listenfd) {
				// 有客户端连接进来
				struct sockaddr_in client_address;
				socklen_t clinent_addrlen = sizeof(client_address);
				int connfd = accept(listenfd, (struct sockaddr*)&client_address, &clinent_addrlen);
				if ( connfd < 0 ) {
					printf( "errno is: %d\n", errno );
					continue;
                }

				if(http_conn::m_user_count >= MAX_FD) {
					// 目前的连接数满了
					// 给客户端写一个信息：服务器内部正忙。
					close(connfd);
					continue;
				}

				// 将新的客户的数据初始化,放到数据
				users[connfd].init(connfd, client_address);
				
				// 对方异常断开或者错误等事件
			} else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
				users[sockfd].close_conn();
			} else if(events[i].events & EPOLLIN) {
				if(users[sockfd].read()) {
					// 一次性把所有数据都读完
					pool->append(users + sockfd);
				} else {
					users[sockfd].close_conn();
				}
			} else if(events[i].events & EPOLLOUT) {
				if(!users[sockfd].write()) {
					users[sockfd].close_conn();
				}
			}
		}

	}

	close(epollfd);
	close(listenfd);
	delete []users;
	delete pool;

    return 0;
}
