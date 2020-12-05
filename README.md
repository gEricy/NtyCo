## NtyCo

该协程库，相较于微信libco，实现原理一模一样，采用IO多路复用的事件驱动与协程切换发挥出巨大的“威力”

不同点：NtyCo没有自己封装实现自己的【条件变量Cond】

建议：初学者建议读NtyCo，之后，可以再读微信协程库Libco

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
