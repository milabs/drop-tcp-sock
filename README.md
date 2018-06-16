# 0

This module allows one to drop TCP connections and can be usefull for killing `TIME-WAIT` sockets.

# usage

First compile and load the module:
~~~
make
insmod ./drop-tcp-sock.ko
~~~

Single socket killing:
~~~
# netstat -n | grep WAIT
tcp        0      0 127.0.0.1:50866             127.0.0.1:22                TIME_WAIT

# echo "127.0.0.1:50866 127.0.0.1:22" >/proc/net/tcpdropsock
~~~

Multiple sockets killing:
~~~
# netstat -n | grep WAIT | awk '{print $4"\t"$5}'
127.0.0.1:41278	127.0.0.1:22
127.0.0.1:41276	127.0.0.1:22
127.0.0.1:41274	127.0.0.1:22

# netstat -n | grep WAIT | awk '{print $4"\t"$5}' >/proc/net/tcpdropsock
~~~

# features

- 2.6.32+ kernels
- network namespaces support
- batch socket killing (multiple at once)

# credits

Original idea: [Roman Arutyunyan](https://github.com/arut)

This module implementation: [Ilya V. Matveychikov](https://github.com/milabs)

2018
