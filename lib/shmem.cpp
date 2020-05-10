#include <QSharedMemory>
#include <QLatin1String>

// Multiple instances: KK1D, 17 Jul 2013
QSharedMemory shmem;

struct jt9com;

// C wrappers for a QSharedMemory class instance
extern "C"
{
  bool shmem_create (int nsize) {return shmem.create(nsize);}
  void shmem_setkey (char * const mykey) {shmem.setKey(QLatin1String{mykey});}
  bool shmem_attach () {return shmem.attach();}
  int shmem_size () {return static_cast<int> (shmem.size());}
  struct jt9com * shmem_address () {return reinterpret_cast<struct jt9com *>(shmem.data());}
  bool shmem_lock () {return shmem.lock();}
  bool shmem_unlock () {return shmem.unlock();}
  bool shmem_detach () {return shmem.detach();}
}
