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
make -j
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


## Options

### Verbose:

To run the agent in verbose mode call:

```
./main --verbose
```

In verbose mode, the agent will output the utterances it is matching on in the command line output.

### Logging Filemode:

To make the agent produce a log file, call:

```
./main --verbose_file
```

Be aware that this is not compatible with --verbose. Under this option, the agent will create a /logs/ directory in the root directory of the agent. It will write a log file in that directory containing in-depth information on the cdc-events published on the bus.
