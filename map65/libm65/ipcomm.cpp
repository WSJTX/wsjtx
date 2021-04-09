#include <QDebug>
#include <qsharedmemory.h>
#include <QSystemSemaphore>

QSharedMemory mem_m65("mem_m65");
QSystemSemaphore sem_m65("sem_m65", 1, QSystemSemaphore::Open);

extern "C" {
  bool attach_m65_();
  bool create_m65_(int nsize);
  bool detach_m65_();
  bool lock_m65_();
  bool unlock_m65_();
  char* address_m65_();
  int size_m65_();

  bool acquire_m65_();
  bool release_m65_();

  extern struct {
    char c[10];
  } m65com_;
}

bool attach_m65_() {return mem_m65.attach();}
bool create_m65_(int nsize) {return mem_m65.create(nsize);}
bool detach_m65_() {return mem_m65.detach();}
bool lock_m65_() {return mem_m65.lock();}
bool unlock_m65_() {return mem_m65.unlock();}
char* address_m65_() {return (char*)mem_m65.constData();}
int size_m65_() {return (int)mem_m65.size();}

bool acquire_m65_() {return sem_m65.acquire();}
bool release_m65_() {return sem_m65.release();}
