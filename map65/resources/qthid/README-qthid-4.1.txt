1. INTRODUCTION

Qthid is a small control application for the Funcube Dongle devices. It is an evolution of the qthid002 package by Howard Long G6LVB.

This version 4.1 is a special version that adds support for the Funcube Dongle Pro+ (see http://www.funcubedongle.com/?page_id=1073). The original Funcube Dongle Pro is not supported by this version.

Qthid uses the HIDAPI cross platform library by Signal 11 Software (http://www.signal11.us/oss/hidapi/). All the required files are bundled with qthid and no installation of the library is required.


2. INSTALLATION AND USAGE

2.1 Linux

Precompiled binaries (32 or 64 bit) are avaialble for download but users are required to install the Qt runtime libraries on their own. All common Linux distrib utions have the Qt libraries packaged and they can be installed using the package manager for the platform. Qt 4.7 and 4.8 has been tested.

In order to use the Funcube Dongle Pro+ as regular user and udev rule is required. An example funcube-dongle.rules file is included with the package and you can simply copy that file to the /etc/udev/rules.d/ directory (hint: open a terminal and type: "sudo cp funcube-dongle.rules /etc/udev/rules.d/" without the quotes)

Qthid should now detect your Funcube Dongle Pro+ when you plug it in. No reboot or udev restart is necessary.


2.2 Mac OS X (10.6+)

The Mac OS X bundle contains all the necessary Qt libraries and no additional installation or configuration is required. Unlike Linux, the Funcube Dongle will just work.


2.3 Windows

The windows package is self containing and does not require and Qt libs to be installed separately. This release has been tested on Windows 7.


2.4 Building from source

- Install Qt Creator 2.0.1 or newer and Qt SDK 4.7. On recent linux it is normally sufficient to select Qt Creator and the required Qt libraries will be installed as dependencies. On Mac and Windows you need to download the full Qt SDK. On Windows you also need the MS Windows SDK.
- On linux you also need to install the libudev-dev package using the package manager (the name may be different depending on distribution but it should have libudev and dev int he name).
- Open the qthid.pro file in Qt Creator. It will say something about setting up a target; say OK or Finish or Done (depends on version and OS).
- You can now build and execute the project.

It is also possible to build in a terminal:

$ tar xvfz qthid-X.Y.tar.gz
$ cd qthid-X.Y
$ mkdir build
$ cd build
$ qmake ../qthid.pro
$ make

You should now have a qthid binary.


3. License

Qthid is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program.  If not, see <http://www.gnu.org/licenses/>.


The frequency controller widget was taken from Cutesdr by Moe Wheatley, released under the following "Simplified BSD License":

Copyright 2010 Moe Wheatley. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY Moe Wheatley "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL Moe Wheatley OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those of the authors and should not be interpreted as representing official policies, either expressed or implied, of Moe Wheatley.

