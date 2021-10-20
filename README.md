# Zynthian QtQuick Components

A set of QtQuick Components to be used by the Zynthian QML UI

These should be configured to install into somewhere that Qt can pick them up for QML importing. On most
systems, this will mean doing something akin to the following in your clone location:

```
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr
make
sudo make install
```

That should pick up your Qt and ECM installations, and use them to build and install into the expected
locations. If those are missing, you will likely need to install those packages. On zynthian, you can do
this by running:

```
apt install extra-cmake-modules qtbase5-dev kirigami2-dev qtdeclarative5-dev rtmidi-devel
```

Once installed, you should be able to use the components simply by adding something like the following to
your qml files:

```
import org.zynthian.quick 1.0 as ZynQuick

ZynQuick.PlayGrid {
    id: component
    name: "My Awesome PlayGrid"
    // ...the rest of your implementation goes here (or just use the BasePlayGrid component from zynthian-qml)
}

```
