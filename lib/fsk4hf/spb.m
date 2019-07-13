clear all;
global N
global R
global A

#-------------------------------------------------------------------------------
function retval = f1(theta)
  global N;
  global R;
  retval=0.0;
  gterm = gammaln(N/2) - gammaln((N+1)/2) - log(2*sqrt(pi));
  rhs = -N*R*log(2);
  lhs=gterm + (N-1)*log(sin(theta)) + log(1-(tan(theta).^2)/N) - log(cos(theta));
  retval = rhs-real(lhs);
endfunction
#-------------------------------------------------------------------------------

#-------------------------------------------------------------------------------
function retval = d(N,i,x)
  t1=(x.^2)/2;
  t2=gammaln(N/2);
  t3=-gammaln(i/2+1);
  t4=-gammaln(N-i);
  t5=(N-1-i)*log(sqrt(2)*x);
  t6=-log(2)/2;
  t7arg=1+(-1)^i * gammainc((x.^2)/2.0,(i+1)/2);
  t7=log(t7arg);
  retval=t1+t2+t3+t4+t5+t6+t7;
endfunction
#-------------------------------------------------------------------------------

#-------------------------------------------------------------------------------
function retval = maxstar(x1,x2)
  retval = max(x1,x2)+log(1+exp(-abs(x1-x2)));
endfunction
#-------------------------------------------------------------------------------

#-------------------------------------------------------------------------------
function retval = spb_integrand(x)
 global N;
 global A;

 t1=log(N-1);
 t2=-N*(A^2)/2;
 t3=-0.5*log(2*pi);
 t4=(N-2)*log(sin(x));

 arg=sqrt(N)*A*cos(x);
 t5=maxstar(d(N,0,arg),d(N,1,arg));
 for i=2:N-1
   t5=maxstar(t5,d(N,i,arg));
 endfor

 retval=exp(t1+t2+t3+t4+t5);
endfunction
#-------------------------------------------------------------------------------

#-------------------------------------------------------------------------------
function retval = qfunc(x)
  retval = 0.5 * erfc(x/sqrt(2));
endfunction
#-------------------------------------------------------------------------------

#-------------------------------------------------------------------------------
# Calculate sphere packing lower bound on the probability of word error
# given block length (N), code rate (R), and Eb/No.
# 
# Ref: 
# "Log-Domain Calculation of the 1959 Sphere-Packing Bound with Application to 
# M-ary PSK Block Coded Modulation," Igal Sason and Gil Weichman, 
# doi: 10.1109/EEEI.2006.321097
#-------------------------------------------------------------------------------
N=128
K=90
R=K/N

delta=0.01;
[ths,fval,info,output]=fzero(@f1,[delta,pi/2-delta], optimset ("jacobian", "off"));

for ebnodb=-3:0.5:4
  ebno=10^(ebnodb/10.0);
  esno=ebno*R;
  A=sqrt(2*esno);
  term1=quadcc(@spb_integrand,ths,pi/2);
  term2=qfunc(sqrt(N)*A);
  pe=term1+term2;
  ps=1-pe;
  printf("%f %f\n",ebnodb,ps);
endfor
