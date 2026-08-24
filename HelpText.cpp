#include "HelpText.hpp"
#include <QCoreApplication>

namespace Radio
{

QString HelpText::keyboardShortcuts()
{
    return QCoreApplication::translate("MainWindow", R"(<table cellspacing=1>
  <tr><td><b>Esc      </b></td><td>Stop Tx, abort QSO, clear next-call queue</td></tr>
  <tr><td><b>F1       </b></td><td>Online User's Guide (Alt: transmit Tx6)</td></tr>
  <tr><td><b>Shift+F1  </b></td><td>Copyright Notice</td></tr>
  <tr><td><b>Ctrl+F1  </b></td><td>About WSJT-X</td></tr>
  <tr><td><b>F2       </b></td><td>Open settings window (Alt: transmit Tx2)</td></tr>
  <tr><td><b>F3       </b></td><td>Display keyboard shortcuts (Alt: transmit Tx3)</td></tr>
  <tr><td><b>F4       </b></td><td>Clear DX Call, DX Grid, Tx messages 1-4 (Alt: transmit Tx4)</td></tr>
  <tr><td><b>Alt+F4   </b></td><td>Exit program</td></tr>
  <tr><td><b>F5       </b></td><td>Display special mouse commands (Alt: transmit Tx5)</td></tr>
  <tr><td><b>F6       </b></td><td>Open next file in directory (Alt: cycle CQ response mode)</td></tr>
  <tr><td><b>Shift+F6 </b></td><td>Decode all remaining files in directory</td></tr>
  <tr><td><b>F7       </b></td><td>Display Message Averaging window</td></tr>
  <tr><td><b>F11      </b></td><td>Move Rx frequency down 1 Hz</td></tr>
  <tr><td><b>Ctrl+F11 </b></td><td>Move identical Rx and Tx frequencies down 1 Hz</td></tr>
  <tr><td><b>Shift+F11 </b></td><td>Move Tx frequency down 60 Hz (FT8) or 90 Hz (FT4)</td></tr>
  <tr><td><b>Ctrl+Shift+F11 </b></td><td>Move dial frequency down 1000 Hz</td></tr>
  <tr><td><b>F12      </b></td><td>Move Rx frequency up 1 Hz</td></tr>
  <tr><td><b>Ctrl+F12 </b></td><td>Move identical Rx and Tx frequencies up 1 Hz</td></tr>
  <tr><td><b>Shift+F12 </b></td><td>Move Tx frequency up 60 Hz (FT8) or 90 Hz (FT4)</td></tr>
  <tr><td><b>Ctrl+Shift+F12 </b></td><td>Move dial frequency up 1000 Hz</td></tr>
  <tr><td><b>Alt+1-6  </b></td><td>Set now transmission to this number on Tab 1</td></tr>
  <tr><td><b>Ctl+1-6  </b></td><td>Set next transmission to this number on Tab 1</td></tr>
  <tr><td><b>Alt+A    </b></td><td>Clear Active Stations for QMAP</td></tr>
  <tr><td><b>Alt+B    </b></td><td>Toggle "Best S+P" status</td></tr>
  <tr><td><b>Alt+C    </b></td><td>Cycle CQ response mode</td></tr>
  <tr><td><b>Alt+D    </b></td><td>Decode again at QSO frequency</td></tr>
  <tr><td><b>Shift+D  </b></td><td>Full decode (both windows)</td></tr>
  <tr><td><b>Ctrl+E   </b></td><td>Turn on TX even/1st</td></tr>
  <tr><td><b>Shift+E  </b></td><td>Turn off TX even/1st</td></tr>
  <tr><td><b>Alt+E    </b></td><td>Erase</td></tr>
  <tr><td><b>Ctrl+F   </b></td><td>Edit the free text message box</td></tr>
  <tr><td><b>Alt+G    </b></td><td>Generate standard messages</td></tr>
  <tr><td><b>Alt+H    </b></td><td>Halt Tx</td></tr>
  <tr><td><b>Ctrl+I   </b></td><td>Add Dx Call to the Ignore List</td></tr>
  <tr><td><b>Ctrl+L   </b></td><td>Lookup callsign in database, generate standard messages</td></tr>
  <tr><td><b>Alt+M    </b></td><td>Monitor</td></tr>
  <tr><td><b>Alt+N    </b></td><td>Toggle "Enable Tx"</td></tr>
  <tr><td><b>Ctrl+O   </b></td><td>Open a .wav file</td></tr>
  <tr><td><b>Alt+O    </b></td><td>Change operator</td></tr>
  <tr><td><b>Alt+Q    </b></td><td>Open "Log QSO" window</td></tr>
  <tr><td><b>Ctrl+R   </b></td><td>Set Tx4 message to RRR (not in FT4)</td></tr>
  <tr><td><b>Alt+R    </b></td><td>Set Tx4 message to RR73</td></tr>
  <tr><td><b>Ctrl+Shift+R  </b></td><td>Refresh Active Stations window</td></tr>
  <tr><td><b>Alt+S    </b></td><td>Stop monitoring</td></tr>
  <tr><td><b>Alt+T    </b></td><td>Toggle Tune status</td></tr>
  <tr><td><b>Ctrl+X       </b></td><td>Open next file in directory</td></tr>
  <tr><td><b>Alt+x </b></td><td>Decode all remaining files in directory</td></tr>
  <tr><td><b>Alt+Z    </b></td><td>Clear hung decoder status</td></tr>

</table>)");
}

QString HelpText::specialMouseCommands()
{
    return QCoreApplication::translate("MainWindow", R"(<table cellpadding=5>
  <tr>
    <th align="right">Click on</th>
    <th align="left">Action</th>
  </tr>
  <tr>
    <td align="right">Waterfall:</td>
    <td><b>Click</b> to set Rx frequency.<br/>
        <b>Right-click</b> to set Tx frequency.<br/>
        <b>Double-right-click</b> to set Rx and Tx frequencies.
    </td>
  </tr>
  <tr>
    <td align="right">Decoded text:</td>
    <td><b>Double-click</b> to copy second callsign to Dx Call, locator to Dx Grid, change Rx<br/>
        and Tx frequency to decoded signal's frequency, and generate standard messages.<br/>
        Hold down <b>Alt</b> to prevent Tx from being enabled on <b>double-click</b>.<br/>
        If <b>Hold Tx Freq</b> is checked or first callsign in message is your<br/>
        own call, Tx frequency is not changed unless <b>Ctrl</b> is held down.
    </td>
  </tr>
  <tr>
    <td align="right">Dial Frequency:</td>
    <td><b>Turn the mouse wheel</b> to change the kHz values, or:<br/>
        <b>Right-click</b> to increase frequency by 1 kHz.<br/>
        <b>Left-click</b> to decrease frequency by 1 kHz.<br/>
        The mouse pointer must be over the Dial Frequency indicator.
    </td>
  </tr>
  <tr>
    <td align="right">H Button:</td>
    <td><b>Click</b> to toggle FT8 Hound Mode On/Off.<br/>
        <b>Right-click</b> to activate or deactivate SuperFox mode.
    </td>
  </tr>
  <tr>
    <td align="right">FT8 Button:</td>
    <td><b>Click</b> to switch to FT8 Hound Mode.<br/>
        <b>Right-click</b> to toggle last used Special Operating Activity On/Off.
    </td>
  </tr>
  <tr>
    <td align="right">Q65 Button:</td>
    <td><b>Click</b> to switch to Q65 Mode.<br/>
        <b>Right-click</b> to switch to Q65 Pileup Mode.
    </td>
  </tr>
  <tr>
    <td align="right">JT65 Button:</td>
    <td><b>Click</b> to switch to JT65 Mode.<br/>
        <b>Right-click</b> to switch to JT9 Mode.
    </td>
  </tr>
  <tr>
    <td align="right">Tx5 Button:</td>
    <td><b>Right-click</b> to retain Tx5 free text.
    </td>
  </tr>
  <tr>
    <td align="right">Tx Even/1st:</td>
    <td><b>Right-click</b> to freeze the state of the checkbox.<br/>
        <b>Right-click on the FT8 Button</b> to unfreeze.
    </td>
  </tr>
  <tr>
    <td align="right">Enable Tx Button:</td>
    <td><b>Click</b> to toggle Auto-Tx mode On/Off.<br/>
        <b>Right-click</b> to toggle Wait & Pounce On/Off after selecting<br/>
        any CQ response mode other than CQ: None.
    </td>
  </tr>
  <tr>
    <td align="right">Erase button:</td>
    <td><b>Click</b> to erase Rx Frequency window.<br/>
        <b>Double-click</b> to erase Rx Frequency and Band Activity windows.<br/>
        If <b>Alternate Erase button behavior</b> is checked:<br/>
        <b>Click</b> to erase Band Activity window.<br/>
        <b>Right-click</b> to erase Rx Frequency window.<br/>
        <b>Double-click</b> to erase Rx Frequency and Band Activity windows.
    </td>
  </tr>
  <tr>
    <td align="right">DX Call Button:</td>
    <td><b>Click</b> to toggle Wait & Call On/Off.<br/>
        <b>Right-click</b> to clear the Dx Call, Dx Grid and Std Msgs.
    </td>
  </tr>
  <tr>
    <td align="right">Lookup Button:</td>
    <td><b>Click</b> to search for callsign in database.<br/>
        <b>Right-click</b> to search for Dx Call on qrz.com.
    </td>
  </tr>
  <tr>
    <td align="right">Add Button:</td>
    <td><b>Click</b> to add callsign and locator to database.<br/>
        <b>Right-click</b> to search for Dx Call on hamqth.com.
    </td>
  </tr>
  <tr>
    <td align="right">Ignore Button:</td>
    <td><b>Click</b> to add callsign to the Ignore List.<br/>
       <b>Right-click</b> to search for Dx Call on qrzcq.com.
    </td>
  </tr>
  <tr>
  <td align="right">Band Buttons:</td>
  <td><b>Click</b> to toggle band / mode default frequencies.<br/>
      <b>Right-click</b> to toggle FT8 DXpedition frequencies.
    </td>
  </tr>
</table>)");
}

QString HelpText::prefixes()
{
    return QCoreApplication::translate("MainWindow", R"(Type 1 Prefixes:

 1A    1S    3A    3B6   3B8   3B9   3C    3C0   3D2   3D2C  3D2R  3DA   3V    3W    3X   
 3Y    3YB   3YP   4J    4L    4S    4U1I  4U1U  4W    4X    5A    5B    5H    5N    5R   
 5T    5U    5V    5W    5X    5Z    6W    6Y    7O    7P    7Q    7X    8P    8Q    8R   
 9A    9G    9H    9J    9K    9L    9M2   9M6   9N    9Q    9U    9V    9X    9Y    A2   
 A3    A4    A5    A6    A7    A9    AP    BS7   BV    BV9   BY    C2    C3    C5    C6   
 C9    CE    CE0X  CE0Y  CE0Z  CE9   CM    CN    CP    CT    CT3   CU    CX    CY0   CY9  
 D2    D4    D6    DL    DU    E3    E4    EA    EA6   EA8   EA9   EI    EK    EL    EP   
 ER    ES    ET    EU    EX    EY    EZ    F     FG    FH    FJ    FK    FKC   FM    FO   
 FOA   FOC   FOM   FP    FR    FRG   FRJ   FRT   FT5W  FT5X  FT5Z  FW    FY    M     MD   
 MI    MJ    MM    MU    MW    H4    H40   HA    HB    HB0   HC    HC8   HH    HI    HK   
 HK0A  HK0M  HL    HM    HP    HR    HS    HV    HZ    I     IS    IS0   J2    J3    J5   
 J6    J7    J8    JA    JDM   JDO   JT    JW    JX    JY    K     KG4   KH0   KH1   KH2  
 KH3   KH4   KH5   KH5K  KH6   KH7   KH8   KH9   KL    KP1   KP2   KP4   KP5   LA    LU   
 LX    LY    LZ    OA    OD    OE    OH    OH0   OJ0   OK    OM    ON    OX    OY    OZ   
 P2    P4    PA    PJ2   PJ7   PY    PY0F  PT0S  PY0T  PZ    R1F   R1M   S0    S2    S5   
 S7    S9    SM    SP    ST    SU    SV    SVA   SV5   SV9   T2    T30   T31   T32   T33  
 T5    T7    T8    T9    TA    TF    TG    TI    TI9   TJ    TK    TL    TN    TR    TT   
 TU    TY    TZ    UA    UA2   UA9   UK    UN    UR    V2    V3    V4    V5    V6    V7   
 V8    VE    VK    VK0H  VK0M  VK9C  VK9L  VK9M  VK9N  VK9W  VK9X  VP2E  VP2M  VP2V  VP5  
 VP6   VP6D  VP8   VP8G  VP8H  VP8O  VP8S  VP9   VQ9   VR    VU    VU4   VU7   XE    XF4  
 XT    XU    XW    XX9   XZ    YA    YB    YI    YJ    YK    YL    YN    YO    YS    YU   
 YV    YV0   Z2    Z3    ZA    ZB    ZC4   ZD7   ZD8   ZD9   ZF    ZK1N  ZK1S  ZK2   ZK3  
 ZL    ZL7   ZL8   ZL9   ZP    ZS    ZS8   KC4   E5   

Type 1 Suffixes:    /0 /1 /2 /3 /4 /5 /6 /7 /8 /9 /A /P)");
}

}
