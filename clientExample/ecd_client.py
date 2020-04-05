import socket

class ecd_client(object):
    "Simple class which handles connections to an EtherCat daemon"

    sock = None
    ip   = None
    port = None

    isReady = None #Ready for next command

    __BUFFLEN = 1024

    def __init__(self, ip=socket._LOCALHOST, port=4200):
        self.ip = ip
        self.port = port

        self.isReady = False

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.ip,self.port))

        self.doRead()
        assert self.isReady

    def doRead(self):
        "Read from the socket until an ok is found"
        istr_complete = b''

        while True: #Recieve data untill 'ok\n'
            istr = self.sock.recv(self.__BUFFLEN)
            istr = istr.rstrip(b'\0')
            #print(istr)
            istr_complete += istr
            if istr.endswith(b'ok\n'):
                break
        self.isReady = True

        ilines = istr_complete.split(b'\n')
        ilines_filtered = []

        for l in ilines:
            if l == b'ok' or l == b'':
                continue
            if l[:3] == b'err':
                raise ecd_error
            assert l[:2] == b'  ', "Expect first two chars of response to be blank, got '{}'".format(l)

            ilines_filtered.append(l.strip(b' '))
            #print (ilines_filtered[-1])

        return ilines_filtered

    def call_metaAll(self):
        self.sock.send(b'meta all')
        resp = self.doRead()
        print(resp)

        # TODO: Parse it in a meaningfull way...

    def call_get(self, slave, idx, subidx):
        address = bytes("{:d}:0x{:04x}:0x{:02x}".format(slave,idx,subidx), 'ascii')
        self.sock.send(b'get '+address)
        
        resp = self.doRead()
        assert len(resp) == 1

        rs = resp[0].split()
        typeName = rs[-1]
        if typeName.startswith(b'INTEGER') or typeName.startswith(b'UNSIGNED'): 
            return int(rs[1])
        elif typeName.startswith(b'REAL'):
            return float(rs[0])

    def __del__(self):
        self.sock.send(b'bye')
        istr = self.sock.recv(self.__BUFFLEN)

        if (istr != b'bye\n'):
            print("WARNING: Got unexpected close message '{}'".format(istr))

        self.sock.close()

class ecd_error(Exception):
    pass