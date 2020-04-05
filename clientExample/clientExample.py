#! /usr/bin/env python3

# Example EtherCatDaemon client, which logs the temperature every second and plots it

from ecd_client import ecd_client,ecd_error

import matplotlib.pyplot as plt
import matplotlib

import time
import datetime

import sys

if __name__ == "__main__":
    "Example demonstrating how ecd_client can be used"

    host = 'localhost'
    if len(sys.argv) == 2:
        host = sys.argv[1]

    cli = ecd_client(host)

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


