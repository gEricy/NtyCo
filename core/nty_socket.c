/*
 *  Author : WangBoJing , email : 1989wangbojing@gmail.com
 * 
 *  Copyright Statement:
 *  --------------------
 *  This software is protected by Copyright and the information contained
 *  herein is confidential. The software may not be copied and the information
 *  contained herein may not be used or disclosed except with the written
 *  permission of Author. (C) 2017
 * 
 *

****       *****                                      *****
  ***        *                                       **    ***
  ***        *         *                            *       **
  * **       *         *                           **        **
  * **       *         *                          **          *
  *  **      *        **                          **          *
  *  **      *       ***                          **
  *   **     *    ***********    *****    *****  **                   ****
  *   **     *        **           **      **    **                 **    **
  *    **    *        **           **      *     **                 *      **
  *    **    *        **            *      *     **                **      **
  *     **   *        **            **     *     **                *        **
  *     **   *        **             *    *      **               **        **
  *      **  *        **             **   *      **               **        **
  *      **  *        **             **   *      **               **        **
  *       ** *        **              *  *       **               **        **
  *       ** *        **              ** *        **          *   **        **
  *        ***        **               * *        **          *   **        **
  *        ***        **     *         **          *         *     **      **
  *         **        **     *         **          **       *      **      **
  *         **         **   *          *            **     *        **    **
*****        *          ****           *              *****           ****
                                       *
                                      *
                                  *****
                                  ****



 *
 */

#include "nty_coroutine.h"


// 通过对系统调用进行封装（HOOK），实现调度
// API的实现逻辑，基本都是
//    1. 将事件托管给（epoll所在的调度者）监听: EPOLL_CTL_ADD
//    2. 切到（epoll所在的调度者协程），对该事件进行监听
//    3. 当该事件触发后，切回到上次切走的地方继续执行
//       3.1. 将事件从epoll摘除: EPOLL_CTL_DEL
//       3.2. 执行(真实的系统调用, 如:read/write)
//       3.2. 执行完(真实的系统调用, 如:read/write)之后，??? 


static uint32_t nty_pollevent_2epoll( short events )
{
	uint32_t e = 0;	
	if( events & POLLIN ) 	e |= EPOLLIN;
	if( events & POLLOUT )  e |= EPOLLOUT;
	if( events & POLLHUP ) 	e |= EPOLLHUP;
	if( events & POLLERR )	e |= EPOLLERR;
	if( events & POLLRDNORM ) e |= EPOLLRDNORM;
	if( events & POLLWRNORM ) e |= EPOLLWRNORM;
	return e;
}
static short nty_epollevent_2poll( uint32_t events )
{
	short e = 0;	
	if( events & EPOLLIN ) 	e |= POLLIN;
	if( events & EPOLLOUT ) e |= POLLOUT;
	if( events & EPOLLHUP ) e |= POLLHUP;
	if( events & EPOLLERR ) e |= POLLERR;
	if( events & EPOLLRDNORM ) e |= POLLRDNORM;
	if( events & EPOLLWRNORM ) e |= POLLWRNORM;
	return e;
}


// 将事件托管给epoll，切回到调度器协程
static int nty_poll_inner(struct pollfd *fds, nfds_t nfds, int timeout) {

	if (timeout == 0)
	{
		return poll(fds, nfds, timeout);
	}
	if (timeout < 0)
	{
		timeout = INT_MAX;
	}

	nty_schedule *sched = nty_coroutine_get_sched();
	nty_coroutine *co = sched->curr_thread;
	
	int i = 0;
	for (i = 0;i < nfds;i ++) {
	
		struct epoll_event ev;
		ev.events = nty_pollevent_2epoll(fds[i].events);
		ev.data.fd = fds[i].fd;
		epoll_ctl(sched->poller_fd, EPOLL_CTL_ADD, fds[i].fd, &ev);   // [EPOLL_CTL_ADD]: 将事件添加到epoll中，由epoll触发调度

		co->events = fds[i].events;
		nty_schedule_sched_wait(co, fds[i].fd, fds[i].events, timeout);
	}
	nty_coroutine_yield(co);  // 1. 切走，当前co --> 调度器co

	for (i = 0;i < nfds;i ++) {
	
		struct epoll_event ev;
		ev.events = nty_pollevent_2epoll(fds[i].events);
		ev.data.fd = fds[i].fd;
		epoll_ctl(sched->poller_fd, EPOLL_CTL_DEL, fds[i].fd, &ev);  // [EPOLL_CTL_DEL]: 删除该事件 

		nty_schedule_desched_wait(fds[i].fd);
	}

	return nfds;
}


int nty_socket(int domain, int type, int protocol) {

	int fd = socket(domain, type, protocol);
	if (fd == -1) {
		printf("Failed to create a new socket\n");
		return -1;
	}
	int ret = fcntl(fd, F_SETFL, O_NONBLOCK);
	if (ret == -1) {
		close(ret);
		return -1;
	}
	int reuse = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
	
	return fd;
}

//nty_accept 
//return failed == -1, success > 0

int nty_accept(int fd, struct sockaddr *addr, socklen_t *len) {
	int sockfd = -1;
	int timeout = 1;
	nty_coroutine *co = nty_coroutine_get_sched()->curr_thread;
	
	while (1) {
		struct pollfd fds;
		fds.fd = fd;
		fds.events = POLLIN | POLLERR | POLLHUP;
		nty_poll_inner(&fds, 1, timeout);

		sockfd = accept(fd, addr, len);
		if (sockfd < 0) {
			if (errno == EAGAIN) {
				continue;
			} else if (errno == ECONNABORTED) {
				printf("accept : ECONNABORTED\n");
				
			} else if (errno == EMFILE || errno == ENFILE) {
				printf("accept : EMFILE || ENFILE\n");
			}
			return -1;
		} else {
			break;
		}
	}

	int ret = fcntl(sockfd, F_SETFL, O_NONBLOCK);
	if (ret == -1) {
		close(sockfd);
		return -1;
	}
	int reuse = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
	
	return sockfd;
}


int nty_connect(int fd, struct sockaddr *name, socklen_t namelen) {

	int ret = 0;

	while (1) {

		struct pollfd fds;
		fds.fd = fd;
		fds.events = POLLOUT | POLLERR | POLLHUP;
		nty_poll_inner(&fds, 1, 1);

		ret = connect(fd, name, namelen);
		if (ret == 0) break;

		if (ret == -1 && (errno == EAGAIN ||
			errno == EWOULDBLOCK || 
			errno == EINPROGRESS)) {
			continue;
		} else {
			break;
		}
	}

	return ret;
}

//recv 
// add epoll first
//
ssize_t nty_recv(int fd, void *buf, size_t len, int flags) {
	
	struct pollfd fds;
	fds.fd = fd;
	fds.events = POLLIN | POLLERR | POLLHUP;

	nty_poll_inner(&fds, 1, 1);

	int ret = recv(fd, buf, len, flags);
	if (ret < 0) {
		//if (errno == EAGAIN) return ret;
		if (errno == ECONNRESET) return -1;
		//printf("recv error : %d, ret : %d\n", errno, ret);
		
	}
	return ret;
}


ssize_t nty_send(int fd, const void *buf, size_t len, int flags) {
	
	int sent = 0;

	int ret = send(fd, ((char*)buf)+sent, len-sent, flags);
	if (ret == 0) return ret;
	if (ret > 0) sent += ret;

	while (sent < len) {
		struct pollfd fds;
		fds.fd = fd;
		fds.events = POLLOUT | POLLERR | POLLHUP;

		nty_poll_inner(&fds, 1, 1);
		ret = send(fd, ((char*)buf)+sent, len-sent, flags);
		//printf("send --> len : %d\n", ret);
		if (ret <= 0) {			
			break;
		}
		sent += ret;
	}

	if (ret <= 0 && sent == 0) return ret;
	
	return sent;
}


ssize_t nty_sendto(int fd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen) {


	int sent = 0;

	while (sent < len) {
		struct pollfd fds;
		fds.fd = fd;
		fds.events = POLLOUT | POLLERR | POLLHUP;

		nty_poll_inner(&fds, 1, 1);
		int ret = sendto(fd, ((char*)buf)+sent, len-sent, flags, dest_addr, addrlen);
		if (ret <= 0) {
			if (errno == EAGAIN) continue;
			else if (errno == ECONNRESET) {
				return ret;
			}
			printf("send errno : %d, ret : %d\n", errno, ret);
			assert(0);
		}
		sent += ret;
	}
	return sent;
	
}

ssize_t nty_recvfrom(int fd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen) {

	struct pollfd fds;
	fds.fd = fd;
	fds.events = POLLIN | POLLERR | POLLHUP;

	nty_poll_inner(&fds, 1, 1);

	int ret = recvfrom(fd, buf, len, flags, src_addr, addrlen);
	if (ret < 0) {
		if (errno == EAGAIN) return ret;
		if (errno == ECONNRESET) return 0;
		
		printf("recv error : %d, ret : %d\n", errno, ret);
		assert(0);
	}
	return ret;

}




int nty_close(int fd) {
#if 0
	nty_schedule *sched = nty_coroutine_get_sched();

	nty_coroutine *co = sched->curr_thread;
	if (co) {
		TAILQ_INSERT_TAIL(&nty_coroutine_get_sched()->ready, co, ready_next);
		co->status |= BIT(NTY_COROUTINE_STATUS_FDEOF);
	}
#endif	
	return close(fd);
}



