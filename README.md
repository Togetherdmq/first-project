# webserver-Linux
It's from https://www.nowcoder.com/study/live/504/5/9
跳到webserver目录下，直接编译所有cpp文件，然后运行，附加输入参数端口号a，开启服务器；
使用游览器访问http://主机号：a/index1.html;

测试性能：在webbench1.5目录下，运行webbench：
./webbench -c 1000 -t 5 http://主机号:a/index1.html;
测试1000线程并发在5秒内的请求效率。
