# EtherCAT IP daemon

This code is reading data from an EtherCAT PLC using the SOEM libary.
No control loops etc. are implemented in this code.
This code is intended for research purposes only.

The code is partially based on the example codes in SOEM,
especially simple_example.c and slaveinfo.c.

To test it, start the program, then connect to it with a telnet client in line mode on port 4200.
The 'help' command will get you started.

Licensed under the GNU General Public License version 2 with exceptions.
See LICENSE file in the SOEM root for full license information.

K. Sjobak, 2020.

## Installation

1. Clone the repository:
`git clone https://github.com/kyrsjo/EtherCAT-IPdaemon.git --recursive`

2. Compile:
```cd EtherCAT-IPdaemon
mkdir build
cd build
cmake ..
make
```

3. Run:
`sudo ./daemon eth0`

4. Test (in a different terminal)
`telnet localhost 4200`

5. Test (GUI, in a different terminal)
```cd EtherCAT-IPdaemon
cd clientExample
./clientExample.py```
Please note that if the server is running on a different machine (e.g. a raspberry pi), the client can still connect to it:
`./clientExample.py raspberrypi.local` (or use IP address)