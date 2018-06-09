# 0

This module allows one to drop TCP connections and can be usefull for killing `TIME-WAIT` sockets.

# usage

~~~
# netstat -n | grep WAIT
tcp        0      0 127.0.0.1:50866             127.0.0.1:22                TIME_WAIT

# echo "127.0.0.1:50866 127.0.0.1:22" >/proc/net/tcpdropsock
~~~

# credits

Original idea: [Roman Arutyunyan](https://github.com/arut)

This module implementation: [Ilya V. Matveychikov](https://github.com/milabs)

2018
