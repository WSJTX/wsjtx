
#ifndef UDP_EXPORT_H
#define UDP_EXPORT_H

#ifdef UDP_STATIC_DEFINE
#  define UDP_EXPORT
#  define UDP_NO_EXPORT
#else
#  ifndef UDP_EXPORT
#    ifdef wsjtx_udp_static_EXPORTS
        /* We are building this library */
#      define UDP_EXPORT 
#    else
        /* We are using this library */
#      define UDP_EXPORT 
#    endif
#  endif

#  ifndef UDP_NO_EXPORT
#    define UDP_NO_EXPORT 
#  endif
#endif

#ifndef UDP_DEPRECATED
#  define UDP_DEPRECATED 
#endif

#ifndef UDP_DEPRECATED_EXPORT
#  define UDP_DEPRECATED_EXPORT UDP_EXPORT UDP_DEPRECATED
#endif

#ifndef UDP_DEPRECATED_NO_EXPORT
#  define UDP_DEPRECATED_NO_EXPORT UDP_NO_EXPORT UDP_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef UDP_NO_DEPRECATED
#    define UDP_NO_DEPRECATED
#  endif
#endif

#endif /* UDP_EXPORT_H */
