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


#ifndef __NTY_COROUTINE_H__
#define __NTY_COROUTINE_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include <inttypes.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <netinet/tcp.h>

#include <sys/epoll.h>
#include <sys/poll.h>

#include <errno.h>

#include "nty_queue.h"
#include "nty_tree.h"

#define NTY_CO_MAX_EVENTS		(1024*1024)
#define NTY_CO_MAX_STACKSIZE	(16*1024) // {http: 16*1024, tcp: 4*1024}

#define BIT(x)	 				(1 << (x))
#define CLEARBIT(x) 			~(1 << (x))

#define CANCEL_FD_WAIT_UINT64	1

typedef void (*proc_coroutine)(void *);


typedef enum {
	NTY_COROUTINE_STATUS_WAIT_READ,
	NTY_COROUTINE_STATUS_WAIT_WRITE,
	NTY_COROUTINE_STATUS_NEW,
	NTY_COROUTINE_STATUS_READY,
	NTY_COROUTINE_STATUS_EXITED,
	NTY_COROUTINE_STATUS_BUSY,
	NTY_COROUTINE_STATUS_SLEEPING,
	NTY_COROUTINE_STATUS_EXPIRED,
	NTY_COROUTINE_STATUS_FDEOF,
	NTY_COROUTINE_STATUS_DETACH,
	NTY_COROUTINE_STATUS_CANCELLED,
	NTY_COROUTINE_STATUS_PENDING_RUNCOMPUTE,
	NTY_COROUTINE_STATUS_RUNCOMPUTE,
	NTY_COROUTINE_STATUS_WAIT_IO_READ,
	NTY_COROUTINE_STATUS_WAIT_IO_WRITE,
	NTY_COROUTINE_STATUS_WAIT_MULTI
} nty_coroutine_status;

typedef enum {
	NTY_COROUTINE_COMPUTE_BUSY,
	NTY_COROUTINE_COMPUTE_FREE
} nty_coroutine_compute_status;

typedef enum {
	NTY_COROUTINE_EV_READ,
	NTY_COROUTINE_EV_WRITE
} nty_coroutine_event;


LIST_HEAD(_nty_coroutine_link, _nty_coroutine);
TAILQ_HEAD(_nty_coroutine_queue, _nty_coroutine);

RB_HEAD(_nty_coroutine_rbtree_sleep, _nty_coroutine);
RB_HEAD(_nty_coroutine_rbtree_wait, _nty_coroutine);



typedef struct _nty_coroutine_link nty_coroutine_link;
typedef struct _nty_coroutine_queue nty_coroutine_queue;

typedef struct _nty_coroutine_rbtree_sleep nty_coroutine_rbtree_sleep;
typedef struct _nty_coroutine_rbtree_wait nty_coroutine_rbtree_wait;



// 用户申请了一个stack协程栈
// 还有一个ctx结构体，里面有各种寄存器指针，使用里面的指针bp\sp指向stack的栈顶，ip指向_exec(下一次要调用的函数入口)
// 在switch时，执行寄存器帮我们执行汇编代码实现(保存现场store + 切换load)
//     store: 将当前协程的现场，保存到栈中
//     load : 寄存器切换到目标协程，开始执行

// 寄存器，保存协程上下文
// 上下文切换，就是将 CPU 的寄存器暂时保存，再将即将运行的协程的上下文寄存器，分别
//    mov 到相对应的寄存器上。此时上下文完成切换
typedef struct _nty_cpu_ctx {
	void *esp; // 栈顶
	void *ebp; // 栈基
	void *eip; // 存储 CPU 运行下一条指令的地址（可以把回调函数的地址存储到 EIP 中，将相应的参数存储到相应的参数寄存器中）
	void *edi;
	void *esi;
	void *ebx;
	void *r1;
	void *r2;
	void *r3;
	void *r4;
	void *r5;
} nty_cpu_ctx;

// 调度器对象：全局唯一
typedef struct _nty_schedule {
	uint64_t    birth;  // 协程的创建时间
	nty_cpu_ctx ctx;    // (协程上下文, 保存了寄存器组指令), 
	
	void  *stack;       // (申请的内存) 协程栈：它是让每个协程【独立】的根本！就是把sp指针指向的栈顶位置
	size_t stack_size;  // 当前协程栈的大小（可以自己分配，最小1K，最大一般128K）
    
	int      spawned_coroutines;  // 当前调度器中，一共存在协程的总数（协程创建时，+1；协程释放时，-1）

	uint64_t default_timeout;
    
	struct _nty_coroutine *curr_thread; // 当前正在运行的协程
	int page_size;

    /* 调度器的核心（事件循环）：它是协程调度的驱动/动力 */
	int poller_fd;  // epfd
	struct epoll_event eventlist[NTY_CO_MAX_EVENTS]; // epoll_wait触发时，保存就绪事件
	int nevents;
    int num_new_events;
    
    int eventfd;  // 这个用来干啥 ?


    // 就绪、睡眠、等待
    //     [note] 问过king老师，sleeping/waiting可以使用一棵红黑树
	nty_coroutine_queue         ready;     // 就绪队列
	nty_coroutine_rbtree_sleep  sleeping;  // 睡眠红黑树（sleep函数封装）
	nty_coroutine_rbtree_wait   waiting;   // 等待红黑树（超时事件）

    // pthread_mutex_t defer_mutex;
	// nty_coroutine_queue defer;

	nty_coroutine_link busy;

	//private 
} nty_schedule;

// 协程结构体
typedef struct _nty_coroutine {
	uint64_t     birth;   // 创建时间
	uint64_t     id;      // 协程唯一标识 ID

	nty_cpu_ctx    ctx;   // 协程上下文, 一系列指针 (指向协程栈)
	void          *stack; // 协程栈：是每个协程相互独立的根本（就是sp指针指向的位置）
	
	proc_coroutine func;  // 协程函数，参数
	void          *arg;

	size_t stack_size;    // 还有一个栈大小！

	size_t last_stack_size; 
	
	nty_schedule *sched;          // 协程所属的调度者

    // 协程的3个状态
	nty_coroutine_status status;  

    // 所属的3个状态集合的一个节点
    //    创建时，先放在ready_next队列
    //    设置超时时，从ready_next队列，移动到阻塞队列sleep_node/wait_node
	TAILQ_ENTRY(_nty_coroutine) ready_next;
	RB_ENTRY(_nty_coroutine)         sleep_node;
	RB_ENTRY(_nty_coroutine)         wait_node;

#if CANCEL_FD_WAIT_UINT64
	int fd;
	unsigned short events;  //POLL_EVENT
#else
	int64_t fd_wait;
#endif
	char funcname[64];
	struct _nty_coroutine *co_join;

	void **co_exit_ptr;

	void *ebp;
	uint32_t ops;
	uint64_t sleep_usecs;

    void  *data;

	LIST_ENTRY(_nty_coroutine) busy_next; //

	TAILQ_ENTRY(_nty_coroutine) defer_next;
	TAILQ_ENTRY(_nty_coroutine) cond_next;

	TAILQ_ENTRY(_nty_coroutine) io_next;
	TAILQ_ENTRY(_nty_coroutine) compute_next;

	struct {
		void *buf;
		size_t nbytes;
		int fd;
		int ret;
		int err;
	} io;

	struct _nty_coroutine_compute_sched *compute_sched;
	int ready_fds;
	struct pollfd *pfds;
	nfds_t nfds;
} nty_coroutine;


typedef struct _nty_coroutine_compute_sched {
	nty_cpu_ctx ctx;
	nty_coroutine_queue coroutines;

	nty_coroutine *curr_coroutine;

	pthread_mutex_t run_mutex;
	pthread_cond_t run_cond;

	pthread_mutex_t co_mutex;
	LIST_ENTRY(_nty_coroutine_compute_sched) compute_next;
	
	nty_coroutine_compute_status compute_status;
} nty_coroutine_compute_sched;

extern pthread_key_t global_sched_key;
static inline nty_schedule *nty_coroutine_get_sched(void) {
	return pthread_getspecific(global_sched_key);
}

static inline uint64_t nty_coroutine_diff_usecs(uint64_t t1, uint64_t t2) {
	return t2-t1;
}

static inline uint64_t nty_coroutine_usec_now(void) {
	struct timeval t1 = {0, 0};
	gettimeofday(&t1, NULL);

	return t1.tv_sec * 1000000 + t1.tv_usec;
}



int nty_epoller_create(void);


void nty_schedule_cancel_event(nty_coroutine *co);
void nty_schedule_sched_event(nty_coroutine *co, int fd, nty_coroutine_event e, uint64_t timeout);

void nty_schedule_desched_sleepdown(nty_coroutine *co);
void nty_schedule_sched_sleepdown(nty_coroutine *co, uint64_t msecs);

nty_coroutine* nty_schedule_desched_wait(int fd);
void nty_schedule_sched_wait(nty_coroutine *co, int fd, unsigned short events, uint64_t timeout);

int nty_epoller_ev_register_trigger(void);
int nty_epoller_wait(struct timespec t);
int nty_coroutine_resume(nty_coroutine *co);
void nty_coroutine_free(nty_coroutine *co);
int nty_coroutine_create(nty_coroutine **new_co, proc_coroutine func, void *arg);
void nty_coroutine_yield(nty_coroutine *co);

void nty_coroutine_sleep(uint64_t msecs);


int nty_socket(int domain, int type, int protocol);
int nty_accept(int fd, struct sockaddr *addr, socklen_t *len);
ssize_t nty_recv(int fd, void *buf, size_t len, int flags);
ssize_t nty_send(int fd, const void *buf, size_t len, int flags);
int nty_close(int fd);
int nty_poll(struct pollfd *fds, nfds_t nfds, int timeout);


ssize_t nty_sendto(int fd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t nty_recvfrom(int fd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);








#endif


