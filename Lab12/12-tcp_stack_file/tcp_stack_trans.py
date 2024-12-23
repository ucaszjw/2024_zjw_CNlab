# !/usr/bin/python

import os
import sys
import string
import socket
from time import sleep


data = string.digits + string.ascii_lowercase + string.ascii_uppercase

def server(port, filename):
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    s.bind(('0.0.0.0', int(port)))
    s.listen(3)
    
    cs, addr = s.accept()
    print(addr)

    with open(filename, 'wb') as f:
        while True:
            data = cs.recv(1024)
            if data:
                f.write(data)
            else:
                break
    
    s.close()


def client(ip, port, filename):
    s = socket.socket()
    s.connect((ip, int(port)))

    file_size = os.path.getsize(filename)
    send_size = 0
    
    with open(filename, 'rb') as f:
        while True:
            data = f.read(1024)
            if data:
                send_size += sys.getsizeof(data)
                s.send(data)
            else:
                break
 
    s.close()

if __name__ == '__main__':
    if sys.argv[1] == 'server':
        server(sys.argv[2], "server-output.dat")
    elif sys.argv[1] == 'client':
        client(sys.argv[2], sys.argv[3], "client-input.dat")
