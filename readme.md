# MyTinyWebServer
主要学习了后端的部分，前端部分参考代码实现
使用C++实现的轻量级服务器，可以实现成千上万的连接数目

## 环境
* Linux
* C++14
* MySql

## 全文件的目录树如下
.
└── MyTinyWebServer
    ├── build
    │   └── Makefile
    ├── code        源代码
    │   ├── buffer
    │   │   ├── buffer.cpp
    │   │   ├── buffer.h
    │   │   └── readme.md
    │   ├── http
    │   │   ├── httpconn.cpp
    │   │   ├── httpconn.h
    │   │   ├── httprequest.cpp
    │   │   ├── httprequest.h
    │   │   ├── httpresponse.cpp
    │   │   ├── httpresponse.h
    │   │   └── readme.md
    │   ├── log
    │   │   ├── blockqueue.hpp
    │   │   ├── log.cpp
    │   │   ├── log.h
    │   │   └── readme.md
    │   ├── main.cpp
    │   ├── pool
    │   │   ├── readme.md
    │   │   ├── sqlconnpool.cpp
    │   │   ├── sqlconnpool.h
    │   │   ├── sqlconnRAII.h
    │   │   └── threadpool.h
    │   ├── server
    │   │   ├── epoller.cpp
    │   │   ├── epoller.h
    │   │   ├── readme.md
    │   │   ├── webserver.cpp
    │   │   └── webserver.h
    │   └── timer
    │       ├── heaptimer.cpp
    │       ├── heaptimer.h
    │       └── readme.md
    ├── Makefile
    ├── rss     源文件夹
    │   ├── 400.html
    │   ├── 403.html
    │   ├── 404.html
    │   ├── 405.html
    │   ├── css
    │   │   ├── animate.css
    │   │   ├── bootstrap.min.css
    │   │   ├── font-awesome.min.css
    │   │   ├── magnific-popup.css
    │   │   └── style.css
    │   ├── error.html
    │   ├── fonts
    │   │   ├── FontAwesome.otf
    │   │   ├── fontawesome-webfont.eot
    │   │   ├── fontawesome-webfont.svg
    │   │   ├── fontawesome-webfont.ttf
    │   │   ├── fontawesome-webfont.woff
    │   │   └── fontawesome-webfont.woff2
    │   ├── images
    │   │   ├── favicon.ico
    │   │   ├── instagram-image1.jpg
    │   │   ├── instagram-image2.jpg
    │   │   ├── instagram-image3.jpg
    │   │   ├── instagram-image4.jpg
    │   │   ├── instagram-image5.jpg
    │   │   └── profile-image.jpg
    │   ├── index.html
    │   ├── js
    │   │   ├── bootstrap.min.js
    │   │   ├── custom.js
    │   │   ├── jquery.js
    │   │   ├── jquery.magnific-popup.min.js
    │   │   ├── magnific-popup-options.js
    │   │   ├── smoothscroll.js
    │   │   └── wow.min.js
    │   ├── login.html
    │   ├── picture.html
    │   ├── register.html
    │   ├── video
    │   │   ├── del.mp4
    │   │   └── xxx.mp4
    │   ├── video.html
    │   └── welcome.html
    └── webbench-1.5        压力测试
        ├── Makefile
        ├── socket.c
        ├── webbench
        ├── webbench.c
        └── webbench.o

## 项目启动
先要配置好数据库
```bash
create database yourdb;

use yourdb
create table user(
    username char(50) null,
    password char(50) null
)

insert into user(username,password) values ('name','password');
```

在文件目录下进行编译
```bash
make 
./bin/server
```

删除bin文件夹和log文件夹
```bash
make clean
```

## 压力测试
```bash
./webbench-1.5/webbench -c 10000 -t http://ip:port/
```
