## NtyCo

该协程库，相较于微信libco，实现原理一模一样，采用IO多路复用的事件驱动与协程切换发挥出巨大的“威力”

不同点：NtyCo没有自己封装实现自己的【条件变量Cond】

建议：初学者建议读NtyCo，之后，可以再读微信协程库Libco



#### 协程跳转switch的实现方式

- *setjmp*、*longjmp* 来完成这种类型的分支跳转
- 汇编：寄存器
- 操作系统提供的u_context

#### compile

```
$ make
```


#### server 
```
$ ./bin/nty_server
```
#### client
```
./bin/nty_client
```

#### mul_process, mul_core
```
$ ./bin/nty_server_mulcore
```
#### websocket
```
$ ./bin/nty_websocket_server
```



---

- 展望：协程的多核模式

  1. 借助线程

     > a. 所有的线程共用一个调度器
     >
     > > 会出现线程之间互跳
     >
     > b. 每个线程一个调度器
     >
     > > 成本有点大
     > >
     > > 但是，更好一点（glusterfs的协程就是这种方式！）

  2. 借助进程

     > 让进程挂在某个CPU上，使得该进程只在绑定的CPU上运行（注意：该CPU除了该进程，还可以运行其他进程）
     >
     > ```c
     > // 进程绑定到某个CPU核心
     > int process_bind(void) {
     > 	int num = sysconf(_SC_NPROCESSORS_CONF);
     > 	pid_t self_id = syscall(__NR_gettid);
     > 	cpu_set_t mask;
     > 
     > 	CPU_ZERO(&mask);
     > 	CPU_SET(self_id % num, &mask);
     > 
     > 	sched_setaffinity(0, sizeof(mask), &mask);
     > 	mulcore_entry(9096);
     > }
     > ```
     >
     > 

     > 每个进程一个调度器