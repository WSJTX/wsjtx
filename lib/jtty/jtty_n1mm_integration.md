# JTTY and N1MM Logger+ Integration

## Summary
Summary: WSJT-X in JTTY mode will work with N1MM Logger+ in a similar way to how other RTTY codecs do. Once configured, you can type "JTTY" in the callsign field in N1MM Logger+ to start WSJT-X in JTTY mode. N1MM Logger+ macros will work with JTTY, HOWEVER you will want to optimize the messages for JTTY.
## Setup
- Set Up N1MM Logger+ (version number *TBD* or after) with WSJT-X as you would for the FT8 mode; there are a number of great resources showing how to do this, for example see [Operating WW Digi with N1MM and WSJT-X](https://www.rttycontesting.com/tutorials/n1mm/operating-ww-digi-with-n1mm/)
- Make sure your configuration is working in FT8 mode before attempting to use JTTY, including radio control, PTT, etc. (note that this requires [configuring the "forEW1" configuration](https://www.rttycontesting.com/tutorials/n1mm/operating-ww-digi-with-n1mm/#startingfirsttime))
- Enter JTTY mode by typing "JTTY" in the callsign field in N1MM Logger+ and pressing enter. This will launch WSJT-X in JTTY mode.
- Verify that the WSJT-X window is showing "JTTY" in the footer, and that the WSJT-X window title contains "forEW1"
- Put your radio in test mode, turn the output power to zero, or use a dummy load
- Verify that pressing "F1" in the WSJT-X window, the message "CQ <your callsign> CQ" is being sent by your radio
- Verify that pressing "F1" in the N1MM Logger+ window sends the contents of your F1 macro (JTTY uses the "Digital Function Keys" file)
- [Customize N1MM's "Digital Function Keys" file](https://n1mmwp.hamdocs.com/setup/function-keys/#the-function-key-message-editor) for JTTY operation. Remember to save your old one! See the discussion below for best practices for these messages.

## Best Practices for N1MM Logger+ Digital Function Keys
JTTY can send any message specified for an N1MM Logger+ Digital Function key, within the character set shown in jtty_design.txt. HOWEVER, JTTY has special provisions to send certain messages very efficiently, using 1 to 3 frames.
The special messages are detailed in the `jtty_design.txt` file. The ones you want to use in your N1MM Logger+ Digital Function Keys file are the ones that have a value in the "NF" column of 3 or under.

For example, you could send "CQ TEST DE N9ADG N9ADG TEST". You can see how long it would take to send that message, using the `sjtty` utility included in the wsjtx directory:
```
./sjtty "CQ TEST DE N9ADG N9ADG TEST"
Message after pack/unpack : CQ TEST DE N9ADG N9ADG TEST
   6 frames, Transmission length 10.18 seconds
```
examining the jtty_design.txt file, you can see that sending "CQ N9ADG" will only take 3 frames, and will be sent in 5.08 seconds:
```
./sjtty "CQ N9ADG"
Message after pack/unpack : CQ N9ADG
   2 frames, Transmission length  3.39 seconds
```
But we can do better! If you send "CQ N9ADG CQ", you'll only use 1 frame!
```
 ./sjtty "CQ N9ADG CQ"
Message after pack/unpack : CQ N9ADG CQ
   1 frame, Transmission length  1.70 seconds
```
The name of the contesting game is to send information as quickly as possible, so use `"CQ <callsign> CQ"` for your CQ message.

You should TEST your messages using the `sjtty` utility to see how many frames they will take to send, and how long it will take to send them. The fewer frames, the faster the message will be sent.

We've included an example file (`JTTY Messages.mc`) that uses these special messages. Note that spaces matter. Any change has the potential to make your messages longer. This is not an area where you want to be different.
Also, 599 is ALWAYS the signal report to go fast. Sending any other value will make your message longer.

Note that for the S&P Exchange message, adding the running station's callsign to the message adds 1.7 seconds to the overall transmission. Sending "W9ABC NR?" vs "W9ABC AGN?" adds 1.7 seconds. 



