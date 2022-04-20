# tomcat-CDC

ToMCAT's Coordination Detection Component

This component requires an active message broker. If you do not have one installed you can use mosquitto:

```
sudo port install mosquitto
sudo port load mosquitto
```

To install the prerequisites using MacPorts:

```
sudo port selfupdate
sudo port install cmake boost paho.mqtt.cpp
```

To build:

```
mkdir build
cd build
cmake ..
make -j`
```

To run the program (assuming you are in the build directory)o

```
./main
```

To see available options:

```
./main -h
```

To see the program's output, subscribe your message broker to "agent/tomcat-CDC/coordination_event", like so:

```
mosquitto_sub -t "agent/tomcat-CDC/coordination_event"
```
