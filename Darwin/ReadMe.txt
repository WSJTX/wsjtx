                    Notes on WSJT-X Installation for Mac OS X
                    -----------------------------------------

                               Updated 21 October 2018
                               -----------------------

If you have already downloaded a previous version of WSJT-X then I suggest 
you change the name in the Applications folder from WSJT-X to WSJT-X_previous 
before proceeding.  

If you have installed a previous version of WSJT-X before then there is no 
need to change anything on your system so proceed to NEXT.  

BEGIN:

There are some system matters you must deal with first.  Open a Terminal window
by going to Applications->Utilities and clicking on Terminal.

Along with this ReadMe file there is a file:   sysctl.conf.   Drag this file to your Desktop.

WSJT-X makes use of a block of memory which is shared between different parts of
the code.  The normal allocation of shared memory on a Mac is insufficient and this 
has to be increased.   You should use a Mac editor to examine sysctl.conf.

There are two important parameters that you need to consider.  shmmax determines the
amount of shared memory that must be allocated for WSJT-X to operate.  This is 14680064 (14MB)
and this is defined in the sysctl.conf file and should not be changed.  

It is possible to run more than one instance of WSJT-X simultaneously.  See 
"Section 14. Platform Dependencies" in the User Guide.  The second important parameter 
shmall=17920 determines how many instances are permitted.  This is calculated as: 
  (shmall x 4096/14680064) = 5.
The sysctl.conf file is configured to permit up to 5 instances of wsjtx to run simultaneously.
If this limitation is acceptable then you can continue to install the sysctl.conf file without making any
alterations.  Otherwise you must edit the file to increase shmall according to this calculation.

Now move this file into place for the system to use by typing: (Note this assumes that
you really did drag this file to your Desktop as required earlier.)

  sudo cp "$HOME/Desktop/sysctl.conf" /etc/
  sudo chmod 664 /etc/sysctl.conf
  sudo chown  root:wheel  /etc/sysctl.conf

and then reboot your Mac.  This is necessary to install the changes.  After the
reboot you should re-open the Terminal window as before and you can check that the
change has been made by typing:

  sysctl -a | grep sysv.shm

If shmmax is not shown as 14680064 then contact me since WSJT-X will fail to load with
an error message: "Unable to create shared memory segment".

You are now finished with system changes.  You should make certain that NO error messages
have been produced during these steps.   You can now close the Terminal window.  It will
not be necessary to repeat this procedure again, even when you download an updated
version of WSJT-X.

NEXT:

Drag the WSJT-X app to your preferred location, such as Applications.

You need to configure your sound card.   Visit Applications > Utilities > Audio MIDI 
Setup and select your sound card and then set Format to be "48000Hz 2ch-16bit" for 
input and output.

Now double-click on the WSJT-X app and two windows will appear.  Select Preferences 
under the WSJT-X Menu and fill in various station details on the General panel.   
I recommend checking the 4 boxes under the Display heading and the first 4 boxes under 
the Behaviour heading.

IMPORTANT: If you are using macOS 10.14 (Mojave) it is important to note that the default setting
for audio input is "block".  In order to receive audio from WSJT-X you must visit
System Preferences > Security & Privacy > Privacy and, with WSJT-X launched, select "Microphone"
under Location Services and wsjtx should appear in the panel.   Check the "Allow" box.  You will 
have to quit WSJT-X for this change to take effect.

Next visit the Audio panel and select the Audio Codec you use to communicate between 
WSJT-X and your rig.   There are so many audio interfaces available that it is not 
possible to give detailed advice on selection.  If you have difficulties contact me.   
Note the location of the Save Directory.  Decoded wave forms are located here.

Look at the Reporting panel.  If you check the "Prompt me" box, a logging panel will appear 
at the end of the QSO.  Two log files are provided in Library/Application Support/WSJT-X.
These are a simple wsjtx.log file and wsjtx_log.adi which is formatted for use with 
logging databases.    The "File" menu bar items include a button "Open log directory" 
to open the log directory in Finder for you, ready for processing by any logging 
application you use.

Finally, visit the Radio panel.  WSJT-X is most effective when operated with CAT 
control.  You will need to install the relevant Mac driver for your rig.   This must 
be located in the device driver directory  /dev. You should install your driver 
and then re-launch WSJT-X. Return to the the Radio panel in Preferences and in 
the "Serial port" panel select your driver from the list that is presented.   If 
for some reason your driver is not shown, then insert the full name 
of your driver in the Serial Port panel.   Such as:  /dev/cu.PL2303-00002226 or 
whatever driver you have.  The /dev/ prefix is mandatory.  Set the relevant 
communication parameters as required by your transceiver and click "Test CAT" to
check.

WSJT-X needs the Mac clock to be accurate.  Visit System Preferences > Date & Time 
and make sure that date and time are set automatically.  The drop-down menu will 
normally offer you several time servers to choose from.

On the Help menu, have a look at the new Online User's Guide for operational hints 
and tips.

Please email me if you have problems.

--- John G4KLA     (g4kla@rmnjmn.co.uk)

