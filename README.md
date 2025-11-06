# Vonage Video Linux: Desktop Capture Using X11 Library and Camera capture using OpenCV
A sample where the Linux Desktop (X11) is captured and streamed.

This sample Uses X11 Library to capture the Linux Desktop.
This sample also shows using OpenCV to capture Video from the Camera

Press 'S' to switch between Screen Share and Camera
Press 'Q' to Quit

You will need a valid [Vonage Video API](https://tokbox.com/developer/)
account to build this app. (Note that OpenTok is now the Vonage Video API.)

## Setting up your environment

### OpenTok SDK

Building this sample application requires having a local installation of the
OpenTok Linux SDK.

#### On Debian-based Linuxes

The OpenTok Linux SDK for x86_64 is available as a Debian
package. For Debian we support Debian 9 (Strech) and 10 (Buster). We maintain
our own Debian repository on packagecloud. For Debian 10, follow these steps
to install the packages from our repository.

* Add packagecloud repository:

```bash
curl -s https://packagecloud.io/install/repositories/tokbox/debian/script.deb.sh | sudo bash
```

* Install the OpenTok Linux SDK packages.

```bash
sudo apt install libopentok-dev
```

#### On non-Debian-based Linuxes

Download the OpenTok SDK from [https://tokbox.com/developer/sdks/linux/] (https://tokbox.com/developer/sdks/linux/)
and extract it and set the `LIBOPENTOK_PATH` environment variable to point to the path where you extracted the SDK.
For example:

```bash
wget https://tokbox.com/downloads/libopentok_linux_llvm_x86_64-2.28.0
tar xvf libopentok_linux_llvm_x86_64-2.28.0
export LIBOPENTOK_PATH=<path_to_SDK>
```

## Other dependencies

Before building the sample application you will need to install the following dependencies

### On Debian-based Linuxes

```bash
sudo apt install build-essential cmake clang libc++-dev libc++abi-dev \
    pkg-config libasound2 libpulse-dev libsdl2-dev libopencv-dev
```

### On Fedora

```bash
sudo dnf groupinstall "Development Tools" "Development Libraries"
sudo dnf install SDL2-devel clang pkg-config libcxx-devel libcxxabi-devel cmake opencv opencv-devel
```

## Building and running the C app

Once you have installed the dependencies, you can build the sample application.
Since it's good practice to create a build folder, let's go ahead and create it
in the project directory:


Copy the [config-sample.h](onfig-sample.h) file as `config.h` at
`src/`:

```bash
$ cp config-sample.h config.h
```

Edit the `config.h` file and add your OpenTok API key,
an OpenTok session ID, and token for the floor and translator sessions. For test purposes,
you can obtain a session ID and token from the project page in your
[Vonage Video API](https://tokbox.com/developer/) account. However,
in a production application, you will need to dynamically obtain the session
ID and token from a web service that uses one of
the [Vonage Video API server SDKs](https://tokbox.com/developer/sdks/server/).

Alternatively, if you don't want to bother with the config.h, you can use commandline parameters to run it, see below.

Next, create the building bits using `cmake`:

```bash
$ mkdir build
$ cd build
$ cmake ..
```

Note we are using `clang/clang++` compilers.

Use `make` to build the code:

```bash
$ make
```

When the `connect` binary is built, run it via:

With config.h
```bash
$ ./connect
```

With Commandline Parameters
```bash
$ ./connect <apiKey> <sessionId> <token>
```

You can use the [OpenTok Playground](https://tokbox.com/developer/tools/playground/)
to connect to the OpenTok session in a web browser. This application will only subscribe and get video frames
# valentine_hackathon
