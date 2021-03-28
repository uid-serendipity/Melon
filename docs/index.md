<p align="center"><img width="108" src="https://github.com/Water-Melon/Melon/blob/master/docs/logo.png?raw=true" alt="Melon logo"></p>
<p align="center"><img src="https://img.shields.io/github/license/Water-Melon/Melang" /></p>



欢迎使用Melon C语言库，本库包含了诸多算法、数据结构、功能组件、脚本语言以及实用框架，可便于开发人员依此快速开发应用功能，避免了重复造轮子的窘境。

我们的官方QQ群为：756582294



### 功能

Melon当前提供了如下功能：

- 数据结构
  - 双向链表
  - 斐波那契堆
  - 哈希表
  - 队列
  - 红黑树
  - 栈
- 算法
  - 加密算法：AES 、DES 、3DES 、RC4 、RSA
  - 哈希算法：MD5 、SHA1 、SHA256
  - Base64
  - 大数计算
  - FEC
  - JSON
  - 矩阵运算
  - 里德所罗门编码
  - 正则匹配算法
  - KMP
- 组件
  - 内存池
  - 线程池
  - 数据链
  - TCP 封装
  - 事件机制
  - 文件缓存
  - HTTP 处理
  - 脚本语言
  - 词法分析器
  - websocket
- 脚本语言
  - 抢占式协程语言——Melang
- 框架
  - 多进程模型
  - 多线程模型



### 平台支持

Melon最初是为UNIX系统编写，因此适用于Linux、MacOS等类UNIX系统，并在针对Intel CPU有少量优化。

目前Melon也已经完成了向Windows的初步移植，因此可以在Windows上进行使用。但由于Windows在创建进程上与UNIX系统差异较大，因此导致上述`框架`部分功能在Windows中暂时不支持。



### 目录

- [安装](https://water-melon.github.io/Melon/install.html)
- [快速入门](https://water-melon.github.io/Melon/quickstart.html)