#! /usr/bin/env python3

# Example EtherCatDaemon client, which logs the temperature every second and plots it

#import sys

import socket

import matplotlib.pyplot as plt
import matplotlib
import numpy as np

import time
import datetime

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
                break;
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

if __name__ == "__main__":
    "Example demonstrating how ecd_client can be used"
    cli = ecd_client()

    #cli.call_metaAll()

    #Address of thermometer value
    address_ch = 2
    address_idx = 0x6000
    address_subidx = 0x11

    try:
        temp = cli.call_get(address_ch, address_idx, address_subidx) / 10
        print("Temp right now: {} [degC]".format( temp ) )
    except ecd_error:
        print("Oops! Cable was disconnected?")


    #Start a data logger
    # inspired by https://block.arch.ethz.ch/blog/2016/08/dynamic-plotting-with-matplotlib/
    # and         https://stackoverflow.com/questions/44278369/how-to-keep-matplotlib-python-window-in-background
    print("Data logger/plotter starting. Hit Control+C to abort.")
    timestamps = []
    temps      = []

    plt.ion()
    fig = plt.figure()

    ax = plt.gca()
    line, = ax.plot_date(timestamps, temps, 'r-')
    
    ax.grid()
    
    plt.setp(ax.xaxis.get_majorticklabels(), rotation=45)

    plt.xlabel("Time")
    plt.ylabel("Temperature [degC]")

    plt.show()

    while True:
        now = datetime.datetime.now()
        try:
            temp = cli.call_get(address_ch, address_idx, address_subidx) / 10

            temps.append(temp)
            timestamps.append(now)

            line.set_xdata(timestamps)
            line.set_ydata(temps)

            # recompute the data limits
            ax.relim()
            ax.autoscale_view() 

        except ecd_error:
            print(now,"Loose cable?")

        #Wait 1 second before next poll of hardware
        for i in range(20):
            fig.canvas.flush_events()
            time.sleep(0.05)
        #plt.pause(1.0) #Steals focus

        plt.tight_layout()
 
    # add this if you don't want the window to disappear at the end
    plt.show()


