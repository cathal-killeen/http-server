*******************************************************************************

                                HTTP SERVER
                              Network Systems

*******************************************************************************
Author: Cathal Killeen
Description: An implementation of a HTTP server using C sockets. The server
handles multiple connections using threads. Connections can also be pipelined
if the client specifies the "Connection: Keep-Alive" header in the HTTP request.


Only GET requests have been handled on the server.
