ECEN602 HW3 Programming Assignment 
(implementation of HTTP1.0 proxy server and client)
-----------------------------------------------------------------

Team Number: 28
Member 1 # Singh, Jasmeet (UIN: 523005618)
Member 2 # Zhang, Jiahao  (UIN: 723005723)
---------------------------------------

Description/Comments:
--------------------
Client:
1. Client starts a TCP socket to the proxy server port
2. Client Connects to the proxy server port
3. The URL is parsed to generate the desired filename for storing data, default filename is index.html if no filename is present in the URL requested
4. GET request is generated from the given URL
5. GET request is sent to the proxy server and client starts waiting for the response
6. Client waits till header is being received.
7. After complete header has been received, client starts writing the data into file
8. Client stops after server has disconnected

Server:
1. Server starts a TCP socket and binds it to the input address and port.
2. Server uses select non-blocking I/O method to handle multiple clients.
3. Server receives a new GET Request and the following are the possible cases:
    i) Request Not Present in Cache: Try to get a free cache block, Start a Fetcher socket to get the data for the requester.
    ii) Request present in cache and not stale: Send the data back to the requester from the cache
    iii) Request present in cache but stale: Send a conditional get request with If-Modified-Since header field, Start a Fetcher socket and if it receives a 304 response, update the expire time in cache. But if it receives a 200 response, try to update the cache block and serve back the new data.
4. After the server sends back the data to the client it updates the cache times to maintain LRU timings.
5. If a cache block is being read by some other client, it is allowed to serve more read requests, but if writing is needed it is blocked and new cache block is requested.
6. Server completes the request by storing the intermediate request in a tmp file and then deleting it once the request is complete.
7. If the cache is full and all blocks are busy, the request is served from the intermediate tmp file.

Unix command for starting proxy server:
------------------------------------------
./proxy PROXY_SERVER_IP PROXY_SERVER_PORT

Unix command for starting client:
----------------------------------------------
./client PROXY_SERVER_IP PROXY_SERVER_PORT URL

