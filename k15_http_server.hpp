#ifndef K15_HTTP_SERVER_INCLUDE
#define K15_HTTP_SERVER_INCLUDE

#ifndef AF_INET6
#   define AF_INET6        23              // Internetwork Version 6
#endif

#define K15_HTTP_ARRAY_SIZE(arr) (sizeof(arr)/sizeof(arr)[0])

typedef int socketId;
typedef unsigned char kh_uint8;

enum http_callback_result
{
    http_callback_result_handled,
    http_callback_result_not_handled
};

struct http_client
{
    socketId socket;
};

struct http_request
{
    const char*             pVersion;
    const char*             pMethod;
    const char*             pPath;
    const char*             pBody;
};

typedef http_callback_result (*http_callback_fn)(const http_request*);

enum
{
    HttpFilePathLength = 256,
    HttpTempBufferSizeInBytes = 256,
    HttpRequestBufferSizeInBytes = 4096,
    HttpMaxCallbackCount = 32
};

enum http_server_flag : kh_uint8
{
    http_server_flag_only_serve_below_root = (1 << 0u)
};

struct http_server_memory
{
    char*   pBuffer;
    size_t  bufferSizeInBytes;
    size_t  bufferCapacityInBytes;
};

struct http_request_callback
{
    const char*         pPath;
    http_callback_fn    pFunction;
};

struct http_server
{
    http_server_memory  memory;

    socketId                ipv4Socket;
    socketId                ipv6Socket;
    const char*             pRootDirectory;
    http_request_callback   requestCallbacks[HttpMaxCallbackCount];
    size_t                  rootDirectoryLength;
    int                     port;
    kh_uint8                flags;
};


enum http_status_code : kh_uint8
{
    http_status_code_ok,
    http_status_code_not_found,
    http_status_code_bad_request
};

enum http_error_id : kh_uint8
{
    http_error_id_success,
    http_error_id_out_of_memory,
    http_error_id_socket_error,
    http_error_id_generic,
    http_error_id_io,
    http_error_id_not_found,
    http_error_id_parse_error
};

struct http_server_parameters
{
    const char*     pIpv4BindAddress;
    const char*     pIpv6BindAddress;
    const char*     pRootDirectory;
    char*           pMemoryBuffer;
    size_t          memoryBufferSizeInBytes;
    int             port;
    bool            onlyServeBelowRoot; //FK: Don't allow paths like ../file.txt
};

bool listenOnSocket(const socketId& socket, int protocol, int port, const char* pBindAddress)
{
    if (socket == INVALID_SOCKET)
    {
        return false;
    }

    int bindResult = SOCKET_ERROR;
    if( protocol == AF_INET )
    {
        sockaddr_in sockAddr;
        sockAddr.sin_family = protocol;
        sockAddr.sin_port = htons(port);
        inet_pton(AF_INET, (char*)pBindAddress, (void *)&sockAddr.sin_addr.s_addr);

        bindResult = bind(socket, (const struct sockaddr*)&sockAddr, sizeof(sockAddr));
    }
    else if ( protocol == AF_INET6 )
    {
        sockaddr_in6 sockAddr;
        sockAddr.sin6_family = AF_INET6;
        sockAddr.sin6_port = htons(port);
        sockAddr.sin6_scope_id = 0;

        inet_pton(AF_INET6, (char*)pBindAddress, (void *)&sockAddr.sin6_addr.s6_addr);
        bindResult = bind(socket, (const struct sockaddr*)&sockAddr, sizeof(sockAddr));
    }

    if (bindResult == SOCKET_ERROR)
    {
        int error = WSAGetLastError();
        printf("test%d", error);
        return false;
    }

    const int backlog = 10; //FK: TODO: find reasonable number here
    const int listenResult = listen(socket, backlog);

    if (listenResult == SOCKET_ERROR)
    {
        return false;
    }

    return true;
}

http_client* waitForClientConnection(http_server* pServer)
{
    fd_set readSockets;
    FD_ZERO(&readSockets);
    
    if( pServer->ipv4Socket != INVALID_SOCKET )
    {
        FD_SET(pServer->ipv4Socket, &readSockets);
    }
    
    if( pServer->ipv6Socket != INVALID_SOCKET )
    {
        FD_SET(pServer->ipv6Socket, &readSockets);
    }

    const int selectResult = select(0, &readSockets, nullptr, nullptr, nullptr);
    if (selectResult == -1)
    {
        return nullptr;
    }

    //FK: Temp
    static http_client client;

    if (FD_ISSET(pServer->ipv4Socket, &readSockets))
    {
        client.socket = accept(pServer->ipv4Socket, NULL, NULL);
    }
    else
    {
        client.socket = accept(pServer->ipv6Socket, NULL, NULL);
    }

    return &client;
}

http_error_id receiveClientData(char* pMessageBuffer, size_t* pOutMessageBufferSize, size_t messageBufferCapacityInBytes, http_client* pClient)
{
    char buffer[HttpTempBufferSizeInBytes];
    size_t messageBufferSizeInBytes = 0u;
    while (true)
    {
        const int bytesRead = recv(pClient->socket, buffer, HttpTempBufferSizeInBytes, 0u);

        if (bytesRead == -1)
        {
            return http_error_id_socket_error;
        }
        else if (bytesRead == 0)
        {
            return http_error_id_success;
        }

        if( messageBufferSizeInBytes + bytesRead >= messageBufferCapacityInBytes )
        {
            return http_error_id_out_of_memory;
        }

        memcpy(pMessageBuffer + messageBufferSizeInBytes, buffer, bytesRead);
        messageBufferSizeInBytes += bytesRead;
        *pOutMessageBufferSize = messageBufferSizeInBytes;

        if (bytesRead < HttpTempBufferSizeInBytes)
        {
            return http_error_id_success;
        }
    }

    return http_error_id_success;
}

http_error_id parseHttpRequest( http_request* pOutRequest, char* pMessage, size_t messageLength )
{
    enum parse_state
    {
        parse_state_method,
        parse_state_path,
        parse_state_html_version,
        parse_state_finished
    } state = parse_state_method;

    http_request request;
    request.pMethod = pMessage;

    while( *pMessage != '\0' )
    {
        if( *pMessage == ' ' || *pMessage == '\n' || *pMessage == '\r' )
        {
            if( state == parse_state_method )
            {
                *pMessage = '\0';
                state = parse_state_path;
                request.pPath = ++pMessage;
            }
            else if( state == parse_state_path )
            {
                *pMessage = '\0';
                state = parse_state_html_version;
                request.pVersion = ++pMessage;
            }
            else if( state == parse_state_html_version )
            {
                *pMessage = '\0';
                state = parse_state_finished;
                ++pMessage;
                if( *pMessage == '\n' || *pMessage == '\r')
                {
                    request.pBody = ++pMessage;
                }
                else
                {
                    request.pBody = pMessage;
                }
                break;
            }
        }
        else
        {
            ++pMessage;
        }
    }

    if( state != parse_state_finished )
    {
        return http_error_id_parse_error;
    }

    *pOutRequest = request;
    return http_error_id_success;
}

http_error_id readClientRequest(http_request* pOutRequest, http_client* pClient )
{
    char requestBuffer[HttpRequestBufferSizeInBytes];
    size_t requestBufferSize = 0u;
    const http_error_id receiveResult = receiveClientData( requestBuffer, &requestBufferSize, HttpRequestBufferSizeInBytes, pClient );
    if( receiveResult != http_error_id_success )
    {
        return receiveResult;
    }

    return parseHttpRequest( pOutRequest, requestBuffer, requestBufferSize );
}

void destroyHttpServer(http_server* pServer)
{
    if (pServer->ipv4Socket != INVALID_SOCKET)
    {
        closesocket(pServer->ipv4Socket);
        pServer->ipv4Socket = INVALID_SOCKET;
    }

    if (pServer->ipv6Socket != INVALID_SOCKET)
    {
        closesocket(pServer->ipv6Socket);
        pServer->ipv6Socket = INVALID_SOCKET;
    }
}

const char* convertToPath( const char* pPathToConvert, size_t* pOutPathLength, http_server_memory* pMemory )
{
    char* pConvertedPath = pMemory->pBuffer + pMemory->bufferSizeInBytes;

    size_t targetPathIndex = 0u;
    size_t sourcePathIndex = 0u;
    while( pPathToConvert[ targetPathIndex ] != '\0')
    {
        if( ( targetPathIndex > 0u && pPathToConvert[ targetPathIndex ] == '\\' && pPathToConvert[ targetPathIndex - 1u ] == '\\' ) ||
            pPathToConvert[ targetPathIndex ] == '/' )
        {
            ++targetPathIndex;

            if( pConvertedPath[ sourcePathIndex ] == '/')
            {
                continue;
            }
            else
            {
                if( pMemory->bufferCapacityInBytes - pMemory->bufferSizeInBytes <= sourcePathIndex + 1u )
                {
                    return nullptr;
                }

                pConvertedPath[ sourcePathIndex++ ] = '/';
                continue;
            }
        }

        pConvertedPath[ sourcePathIndex++ ] = pPathToConvert[ targetPathIndex++ ];
    }

    if( pConvertedPath[ sourcePathIndex ] != '/')
    {
        if( pMemory->bufferCapacityInBytes - pMemory->bufferSizeInBytes <= sourcePathIndex + 1u )
        {
            return nullptr;
        }
        pConvertedPath[ sourcePathIndex++ ] = '/';
    }

    *pOutPathLength = sourcePathIndex;
    pConvertedPath[ sourcePathIndex++ ] = '\0';

    pMemory->bufferSizeInBytes += sourcePathIndex;
    return pConvertedPath;
}

http_server* createHttpServer(const http_server_parameters& parameters)
{
    //FK: TEMP:
    static http_server server;
    server.memory.pBuffer                   = parameters.pMemoryBuffer;
    server.memory.bufferCapacityInBytes     = parameters.memoryBufferSizeInBytes;
    server.memory.bufferSizeInBytes         = 0u;
    
    server.rootDirectoryLength              = 0u;
    server.pRootDirectory                   = convertToPath( parameters.pRootDirectory, &server.rootDirectoryLength, &server.memory );
    
    if( server.pRootDirectory == nullptr )
    {
        server.pRootDirectory = "./";
    }

    memset(&server.requestCallbacks, 0u, sizeof(server.requestCallbacks) );

    server.ipv4Socket                       = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    server.ipv6Socket                       = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    server.port                             = parameters.port;

    if (server.ipv4Socket == INVALID_SOCKET && server.ipv6Socket == INVALID_SOCKET)
    {
        destroyHttpServer(&server);
        return nullptr;
    }

    if( server.ipv4Socket != INVALID_SOCKET && !listenOnSocket(server.ipv4Socket, AF_INET, parameters.port, parameters.pIpv4BindAddress))
    {
        destroyHttpServer(&server);
        return nullptr;
    }

    if( server.ipv6Socket != INVALID_SOCKET && !listenOnSocket(server.ipv6Socket, AF_INET6, parameters.port, parameters.pIpv6BindAddress))
    {
        destroyHttpServer(&server);
        return nullptr;
    }

    if( parameters.onlyServeBelowRoot )
    {
        server.flags |= http_server_flag_only_serve_below_root;
    }

    return &server;
}

http_error_id findIndexFileInDirectory( FILE** pOutFileHandle, const char* pBasePath, size_t basePathLength )
{
    static const char* pIndexFileName[] = {
        "index.html",
        "index.htm" 
    };

    static const size_t indexFileNameLength[] = {
        strlen(pIndexFileName[0]),
        strlen(pIndexFileName[1]),
    };

    char filePathBuffer[HttpFilePathLength];
    memcpy(filePathBuffer, pBasePath, basePathLength);

    for (size_t indexFileIndex = 0u; indexFileIndex < K15_HTTP_ARRAY_SIZE(pIndexFileName); ++indexFileIndex)
    {
        memcpy(filePathBuffer + basePathLength, pIndexFileName[indexFileIndex], indexFileNameLength[indexFileIndex]);
        *pOutFileHandle = fopen(filePathBuffer, "rb");
        if( *pOutFileHandle != nullptr )
        {
            return http_error_id_success;
        }
    }

    return http_error_id_not_found;
}

http_error_id sendToClient(http_client* pClient, const char* pBuffer, size_t bufferSizeInBytes)
{
    const int bytesSend = send(pClient->socket, pBuffer, bufferSizeInBytes, 0u);
    if (bytesSend != SOCKET_ERROR)
    {
        return http_error_id_success;
    }

    return http_error_id_generic;
}

http_error_id sendToClient(http_client* pClient, const char* pBuffer)
{
    const size_t bufferSizeInBytes = strlen(pBuffer);
    const int bytesSend = send(pClient->socket, pBuffer, bufferSizeInBytes, 0u);
    if (bytesSend != SOCKET_ERROR)
    {
        return http_error_id_success;
    }

    return http_error_id_generic;
}

http_error_id sendToClient(http_client* pClient, char character)
{
    WSASetLastError(0u);
    const int bytesSend = send(pClient->socket, &character, 1u, 0u);
    if (bytesSend != SOCKET_ERROR)
    {
        return http_error_id_success;
    }

    return http_error_id_generic;
}

http_error_id sendHttpStatusCodeToClient(http_client* pClient, http_status_code statusCode)
{
    switch (statusCode)
    {
        case http_status_code_ok:
        {
            const char message[] = {
                "HTTP/1.1 200 OK\n"
                "Content-Type: text/html\n"
                "Connection: close\n\n" };

            return sendToClient(pClient, message);
        }
        case http_status_code_not_found:
        {
            const char message[] = {
                "HTTP/1.1 404 Not Found\n" };

            return sendToClient(pClient, message);
        }
        case http_status_code_bad_request:
        {
            const char message[] = {
                "HTTP/1.1 400 Bad Request\n" };

            return sendToClient(pClient, message);
        }
    }
    return http_error_id_not_found;
}

http_error_id sendFileContentToClient(http_client* pClient, FILE* pFileHandle)
{
    if( pFileHandle == nullptr )
    {
        return sendHttpStatusCodeToClient(pClient, http_status_code_not_found);
    }
    char buffer[HttpTempBufferSizeInBytes];
    size_t fileOffsetInBytes = 0u;
    while (true)
    {
        const size_t bytesRead = fread(buffer, 1u, HttpTempBufferSizeInBytes, pFileHandle);
        if( ferror(pFileHandle) != 0 )
        {
            return http_error_id_io;
        }

        fileOffsetInBytes += bytesRead;

        const http_error_id sendResult = sendToClient(pClient, buffer, bytesRead);
        if (sendResult != http_error_id_success)
        {
            return sendResult;
        }

        if (bytesRead != HttpTempBufferSizeInBytes)
        {
            break;
        }
    }

    return sendToClient(pClient, "\0\n");
}

void closeClientConnection(http_client* pClient)
{
    closesocket(pClient->socket);
}

http_error_id sendHttpStatusCodeToClientAndCloseConnection(http_client* pClient, http_status_code statusCode)
{
    const http_error_id errorId = sendHttpStatusCodeToClient(pClient, statusCode);
    closeClientConnection(pClient);

    return errorId;
}

bool isDirectoryPath(const char* pPath)
{
    const size_t pathLength = strlen( pPath );
    return pathLength > 0u && pPath[ pathLength - 1u ] == '/';
}

const http_request_callback* findRequestCallback( http_server* pServer, const char* pPath )
{
    for( size_t callbackIndex = 0u; callbackIndex < K15_HTTP_ARRAY_SIZE( pServer->requestCallbacks ); ++callbackIndex )
    {
        if( pServer->requestCallbacks[ callbackIndex].pPath == nullptr || pServer->requestCallbacks[ callbackIndex ].pFunction == nullptr )
        {
            continue;
        }

        if( strcmp(pServer->requestCallbacks[ callbackIndex ].pPath, pPath) == 0u)
        {
            return pServer->requestCallbacks + callbackIndex;
        }
    }

    return nullptr;
}

bool serveHttpClients(http_server* pServer)
{
    while (true)
    {
        http_client* pClient = waitForClientConnection(pServer);
        if (pClient == nullptr)
        {
            continue;
        }

        http_request clientRequest;
        const http_error_id result = readClientRequest( &clientRequest, pClient );
        if (result != http_error_id_success)
        {
            sendHttpStatusCodeToClientAndCloseConnection(pClient, http_status_code_bad_request);
            continue;
        }

        const http_request_callback* pCallback = findRequestCallback( pServer, clientRequest.pPath );
        if( pCallback != nullptr )
        {
            const http_callback_result callbackResult = pCallback->pFunction( &clientRequest );
            if( callbackResult == http_callback_result_handled )
            {
                closeClientConnection(pClient);
                continue;
            }
        }

        if( strcmp( clientRequest.pMethod, "GET" ) == 0 )
        {
            if( isDirectoryPath( clientRequest.pPath ) )
            {
                FILE* pIndexFileHandle = nullptr;
                const http_error_id indexFileResult = findIndexFileInDirectory( &pIndexFileHandle, pServer->pRootDirectory, pServer->rootDirectoryLength );
                if( indexFileResult != http_error_id_success )
                {
                    sendHttpStatusCodeToClientAndCloseConnection(pClient, http_status_code_not_found);
                    continue;
                }

                sendFileContentToClient(pClient, pIndexFileHandle);
                closeClientConnection(pClient);

                fclose(pIndexFileHandle);
                continue;
            }

            FILE* pFileHandle = fopen(clientRequest.pPath, "rb");
            if( pFileHandle == nullptr )
            {
                sendHttpStatusCodeToClientAndCloseConnection(pClient, http_status_code_not_found);
                continue;
            }

            sendFileContentToClient(pClient, pFileHandle);
            closeClientConnection(pClient);
            fclose(pFileHandle);
        }
    }
}

#endif //K15_HTTP_SERVER_INCLUDE