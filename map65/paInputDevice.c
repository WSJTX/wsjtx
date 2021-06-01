#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void paInputDevice(int id, char* hostAPI_DeviceName, int* minChan, 
		   int* maxChan, int* minSpeed, int* maxSpeed)
{
  int i;
  char pa_device_name[128];     
  char pa_device_hostapi[128]; 
  double pa_device_max_speed;
  double pa_device_min_speed;
  int pa_device_max_bytes;
  int pa_device_min_bytes;
  int pa_device_max_channels;
  int pa_device_min_channels;
  char p2[256];
  char *p,*p1;
  static int iret, valid_dev_cnt;

  iret=pa_get_device_info (id,
                          &pa_device_name,
                          &pa_device_hostapi,
			  &pa_device_max_speed,
			  &pa_device_min_speed,
			  &pa_device_max_bytes,
			  &pa_device_min_bytes,
			  &pa_device_max_channels,
			  &pa_device_min_channels);

  if (iret >= 0 ) {
    valid_dev_cnt++;

    p1="";
    p=strstr(pa_device_hostapi,"MME");
    if(p!=NULL) p1="MME";
    p=strstr(pa_device_hostapi,"Direct");
    if(p!=NULL) p1="DirectX";
    p=strstr(pa_device_hostapi,"WASAPI");
    if(p!=NULL) p1="WASAPI";
    p=strstr(pa_device_hostapi,"ASIO");
    if(p!=NULL) p1="ASIO";
    p=strstr(pa_device_hostapi,"WDM-KS");
    if(p!=NULL) p1="WDM-KS";

    sprintf(p2,"%-8s %-39s",p1,pa_device_name);
    for(i=0; i<50; i++) {
      hostAPI_DeviceName[i]=p2[i];
      if(p2[i]==0) break;
    }
    *minChan=pa_device_min_channels;
    *maxChan=pa_device_max_channels;
    *minSpeed=(int)pa_device_min_speed;
    *maxSpeed=(int)pa_device_max_speed;
  }
}
