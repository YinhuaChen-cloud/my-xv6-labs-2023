import socket
import sys

# 这里创建了一个UDP套接字。socket.AF_INET指定了IPv4地址族，socket.SOCK_DGRAM指定了这个套接字是UDP协议的。
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
# 这里定义了服务器将要监听的地址和端口。localhost代表本地机器，sys.argv[1]是从命令行接收的参数，它应该是一个端口号，这里将其转换为整数。
addr = ('localhost', int(sys.argv[1]))
print('listening on %s port %s' % addr, file=sys.stderr)
# 将套接字绑定到上面指定的地址和端口上，这样它就可以接收发送到这个地址和端口的数据包了。
sock.bind(addr)

while True:
    # 这行代码接收客户端发送的数据。recvfrom方法会阻塞，直到有数据到达。4096是接收缓冲区的大小。buf是接收到的数据，raddr是发送数据的客户端的地址。
    buf, raddr = sock.recvfrom(4096)
    # 将接收到的数据解码成UTF-8格式的字符串，并打印到标准错误输出。
    print(buf.decode("utf-8"), file=sys.stderr)
    # 如果接收到数据（buf不为空），则向发送数据的客户端地址发送一条消息“this is the host!”。
    if buf:
        sent = sock.sendto(b'this is the host!', raddr)

