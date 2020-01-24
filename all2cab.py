import sys
from datetime import datetime
mycall="K1JT"
mygrid="FN20"

# Keyed with hiscall:
Freq={}
T0={}
RcvdExch={}
Grid={}

# Keyed with hiscall_band:
Staged={}
TimeLogged={}
QSOinProgress={}
# QSOinProgress bit values:
#  1 He called me
#  2 I called him
#  4 Received his exchange
#  8 Received his Roger
# 16 Sent Roger
# 32 Staged for logging

def isGrid(g):
    """Return True if g is a valid grid4 and not RR73"""
    if len(g)!=4 or g=="RR73": return False
    if ord(g[0:1])<ord('A') or ord(g[0:1])>ord('R'): return False
    if ord(g[1:2])<ord('A') or ord(g[1:2])>ord('R'): return False
    if ord(g[2:3])<ord('0') or ord(g[2:3])>ord('9'): return False
    if ord(g[3:4])<ord('0') or ord(g[3:4])>ord('9'): return False
    return True

if len(sys.argv)!=4:
    print "Usage:   python all2cab.py <mycall> <mygrid> <infile>"
    print "Example: python all2cab.py K1JT FN20 all_wwdigi_2019.txt"
    exit()

f=open(sys.argv[3],mode='r')
ss=f.readlines()
f.close
dt0=datetime.strptime(ss[0][0:13],"%y%m%d_%H%M%S")
iz=len(ss)
nqso=0
hiscall_band=""

for i in range(iz):
    s=ss[i][0:80].strip()
    dt=datetime.strptime(s[0:13],"%y%m%d_%H%M%S")
    tx=" Tx " in s              #True if this is my transmission
    w=s.split()
    if len(w)<10: continue
    if w[7]=="CQ":
        cq=True
        if w[8].isalpha():
            s=s.replace(" CQ "," CQ_")
            w=s.split()
    if len(w)<10: continue
    c1=w[7]
    c2=w[8]
    c3=w[9]
    roger = c3=="R"
    if roger: c3=w[10]
    cq = (c1=="CQ" or c1[0:3]=="CQ_")
    if cq:
        Grid[c2]=c3

    hiscall=""
    if tx and not cq:
        hiscall=c1
    if c1==mycall:
        freq=int(1000.0*float(s[13:23])) + int((int(s[42:47]))/1000)
        hiscall=c2
        Freq[hiscall]=freq
        MHz="%3d" % int(float(s[13:23]))
        hiscall_band=hiscall+MHz
        n=QSOinProgress.get(hiscall_band,0)
        n = n | 1                                 #He called me
        if roger or c3=="RR73" or c3=="RRR" or c3=="73":
            n = n | 8                             # Rcvd Roger
        if isGrid(c3):
            hisgrid=c3
            RcvdExch[hiscall]=hisgrid
            n = n | 4                             #Received his exchange
        else:
            g=Grid.get(hiscall,"")
            if isGrid(g):
                RcvdExch[hiscall]=g
                n = n | 4                         #Received his exchange
        QSOinProgress[hiscall_band]=n
        
    if len(hiscall)>=3:
        MHz="%3d" % int(float(s[13:23]))
        hiscall_band=hiscall+MHz
        if tx:
            n=QSOinProgress.get(hiscall_band,0)
            n = n | 2                             #I called him
            if roger or c3=="RR73" or c3=="RRR" or c3=="73":
                n = n | 4 | 16                    #Rcvd Exch, Sent Roger
            if c3=="RR73" or c3=="RRR" or c3=="73":
                n = n | 8                         #Rcvd Exch, Sent Roger
            QSOinProgress[hiscall_band]=n
        T0[hiscall]=dt

    if len(hiscall_band)<5 or len(hiscall)<3: continue
    
    if QSOinProgress.get(hiscall_band,0)>=31:
        n=QSOinProgress.get(hiscall_band,0)
        n = n | 32
        QSOinProgress[hiscall_band]=n
        t=str(T0[hiscall])[0:16]
        t=t[0:13] + t[14:16]
        buf="QSO: %5d DG %s %-10s    %s       %-10s    %s" % (Freq[hiscall],t,\
                mycall,mygrid,hiscall,RcvdExch.get(hiscall,"    "))
        MHz="%3d" % int(float(s[13:23]))
        hiscall_band=hiscall+MHz
        t=Staged.get(hiscall_band,"")
        time_diff=-1
        if TimeLogged.get(hiscall_band,0)!=0:
            time_diff=(T0[hiscall] - TimeLogged[hiscall_band]).total_seconds()
        if time_diff == -1 or time_diff > 180:    #Log only once within 3 min
            Staged[hiscall_band]=buf              #Staged for logging
            nqso=nqso+1
            TimeLogged[hiscall_band]=T0[hiscall]
            print buf                             #For now, log anything staged
            del QSOinProgress[hiscall_band]
