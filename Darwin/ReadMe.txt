                    Notes on WSJT-X Installation for Mac OS X
                    -----------------------------------------

Important:  If you are using the new Mac with the M1 chip then please read
the section marked:  BEGIN M1.  Otherwise BEGIN INTEL applies.

If you have already downloaded a previous version of WSJT-X then I suggest 
you change the name in the Applications folder from WSJT-X to WSJT-X_previous 
before proceeding.  

I recommend that you follow the installation instructions especially if you
are moving from v2.2 to v2.3 of WSJT-X or you have upgraded macOS.

BEGIN M1:

Double-click on the wsjtx-...-Darwin.dmg file you have downloaded from K1JT's web-site.

Now open a Terminal window by going to Applications->Utilities and clicking on Terminal.

There are two system variables that must be set manually since the M1 Macs do not recognise
automatic parameter settings by means of the sysctl.conf file present in the download.
Type these commands - you will be asked for your password which will not be echoed:

      sudo  sysctl  -w  kern.sysv.shmmax=104857600
      sudo  sysctl  -w  kern.sysv.shmall=25600

It is important to note that these parameter settings will not survive a reboot.  If you
need to reboot your Mac, then these commands must be re-entered.  Now proceed to NEXT.

BEGIN INTEL:

Double-click on the wsjtx-...-Darwin.dmg file you have downloaded from K1JT's web-site.

Now open a Terminal window by going to Applications->Utilities and clicking on Terminal.

Along with this ReadMe file there is a file:   sysctl.conf  which must be copied to a
system area by typing this line in the Terminal window and then pressing the Return key.

      sudo  cp  /Volumes/WSJT-X/sysctl.conf  /etc

you will be asked for your normal password because authorisation is needed to copy this file.
(Your password will not be echoed but press the Return key when completed.)
Now re-boot your Mac. This is necessary to install the changes.  After the
reboot you should re-open the Terminal window as before and you can check that the
change has been made by typing:

      sysctl -a | grep sysv.shm

If shmmax is not shown as 104857600 then contact me since WSJT-X will fail to load with
an error message: "Unable to create shared memory segment".

You can now close the Terminal window.  It will not be necessary to repeat this procedure 
again, even when you download an updated version of WSJT-X.  It might be necessary if you
upgrade macOS.
 
NEXT:

Drag the WSJT-X app to your preferred location, such as Applications.

You need to configure your sound card.   Visit Applications > Utilities > Audio MIDI 
Setup and select your sound card and then set Format to be "48000Hz 2ch-16bit" for 
input and output.

Now double-click on the WSJT-X app and two windows will appear.  Select Preferences 
under the WSJT-X Menu and fill in various station details on the General panel.   
I recommend checking the 4 boxes under the Display heading and the first 4 boxes under 
the Behaviour heading.

Depending on your macOS you might see a pop-up window suggesting that wsjtx wants to use the
microphone.   What this means is that audio input must be allowed.  Agree.

Next visit the Audio panel and select the Audio Codec you use to communicate between 
WSJT-X and your rig.   There are so many audio interfaces available that it is not 
possible to give detailed advice on selection.  If you have difficulties contact me.   
Note the location of the Save Directory.  Decoded wave forms are located here.

Look at the Reporting panel.  If you check the "Prompt me" box, a logging panel will appear 
at the end of the QSO.  Visit Section 11 of the User Guide for information about log files
and how to access them.

Finally, visit the Radio panel.  WSJT-X is most effective when operated with CAT 
control.  You will need to install the relevant Mac device driver for your rig, 
and then re-launch WSJT-X. Return to the Radio panel in Preferences and in 
the "Serial port" panel select your driver from the list that is presented.   If you 
do not know where to get an appropriate driver, contact me.

WSJT-X needs the Mac clock to be accurate.  Visit System Preferences > Date & Time 
and make sure that Date and Time are set automatically.  The drop-down menu will 
normally offer you several time servers to choose from.

On the Help menu, have a look at the new Online User's Guide for operational hints 
and tips and possible solutions to any problem you might have.

Please email me if you have problems.

--- John G4KLA     (g4kla@rmnjmn.co.uk)

Addendum:  Information about sysctl.conf and multiple instances of WSJT-X.

WSJT-X makes use of a block of memory which is shared between different parts of
the code.  The normal allocation of shared memory on a Mac is insufficient and this 
has to be increased.  The sysctl.conf file is used for this purpose.  You can 
use a Mac editor to examine sysctl.conf.  (Do not use another editor - the file 
would probably be corrupted.)

It is possible to run two instances of WSJT-X simultaneously.  See "Section 16.2 
Frequently asked Questions" in the User Guide.  If you wish to run more than two instances
simultaneously, the sysctl.conf file needs to be modified.  Please email me with your
requirements and I will provide a replacement sysctl.conf to suit.

If two instances of WSJT-X are running, it is likely that you might need additional
audio devices, from two rigs for example.  Visit Audio MIDI Setup and create an Aggregate Device
which will allow you to specific more than one interface.  I recommend you consult Apple's guide
on combining multiple audio interfaces which is at https://support.apple.com/en-us/HT202000.  
