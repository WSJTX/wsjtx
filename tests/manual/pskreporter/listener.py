from __future__ import unicode_literals
import sys
import argparse
import logging
import time
import threading
import ipfix.ie
import ipfix.reader
import ipfix.message
import socketserver
import socket

class IPFixDatagramHandler (socketserver.DatagramRequestHandler):

    def handle (self):
        logging.info (f'Connection from {self.client_address}')
        try:
            self.server.msg_buffer.from_bytes (self.packet)
            for rec in self.server.msg_buffer.namedict_iterator ():
                logging.info (f't: {self.server.msg_buffer.get_export_time()}: {rec}')
        except:
            logging.error ('Unexpected exception:', sys.exc_info ()[0])


class IPFixUdpServer (socketserver.UDPServer):

    def __init__ (self, *args, **kwargs):

        self.msg_buffer = ipfix.message.MessageBuffer ()
        super ().__init__ (*args, **kwargs)

class IPFixStreamHandler (socketserver.StreamRequestHandler):

    def handle (self):
        logging.info (f'Connection from {self.client_address}')
        try:
            msg_reader = ipfix.reader.from_stream (self.rfile)
            for rec in msg_reader.namedict_iterator ():
                logging.info (f't: {msg_reader.msg.get_export_time()}: {rec}')
            logging.info (f'{self.client_address} closed their connection')
        except ConnectionResetError:
            logging.info (f'{self.client_address} connection reset')
        except TimeoutError:
            logging.info (f'{self.client_address} connection timed out')
        except:
            logging.error ('Unexpected exception:', sys.exc_info ()[0])
        

if __name__ == "__main__":
    INTERFACE, PORT = '', 4739

    ap = argparse.ArgumentParser (description='Dump IPFIX data collected over UDP')
    ap.add_argument ('-l', '--log', metavar='loglevel', default='WARNING', help='logging level')
    ap.add_argument ('-s', '--spec', metavar='specfile', help='iespec file to read')
    args = ap.parse_args ()

    log_level = getattr (logging, args.log.upper (), None)
    if not isinstance (log_level, int):
        raise ValueError (f'Invalif log level: {log_level}')
    logging.basicConfig (
        level=log_level
        , format='[%(levelname)s] (%(threadName)-10s) %(message)s'
        ,
        )

    logging.info (f'Starting IPFix servers on service port: {PORT}')

    ipfix.ie.use_iana_default ()
    ipfix.ie.use_5103_default ()
    if args.spec:
        ipfix.ie.use_specfile (args.spec)

    udp_server = IPFixUdpServer ((INTERFACE, PORT), IPFixDatagramHandler)
    udp_thread = threading.Thread (name='UDP Server', target=udp_server.serve_forever)

    tcp_server = socketserver.TCPServer ((INTERFACE, PORT), IPFixStreamHandler)
    tcp_server.allow_reuse_address = True
    tcp_server.socket.setsockopt (socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
    tcp_thread = threading.Thread (name='TCP/IP Server', target=tcp_server.serve_forever)

    udp_thread.start ()
    tcp_thread.start ()

    try:
        while True:
            time.sleep (1000)
    except KeyboardInterrupt:
        logging.warning ('Closing down servers')
        udp_server.shutdown ()
        tcp_server.shutdown ()

    udp_thread.join ()
    tcp_thread.join ()
    logging.info ('Servers closed')
