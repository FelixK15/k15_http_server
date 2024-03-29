#ifndef K15_HTTP_SERVER_INCLUDE
#define K15_HTTP_SERVER_INCLUDE

#ifdef _WIN32
#include <winsock2.h>
#include <Ws2tcpip.h>
#endif

#ifndef AF_INET6
#   define AF_INET6        23              // Internetwork Version 6
#endif

#define K15_HTTP_ARRAY_SIZE(arr) (sizeof(arr)/sizeof(arr)[0])

typedef int socketId;

enum http_callback_result
{
    http_callback_result_handled,
    http_callback_result_not_handled
};

enum http_client_flags : unsigned char
{
    http_client_flag_keep_alive = (0 << 1),
    http_client_flag_websocket  = (1 << 1)
};

struct http_client
{
    socketId        socket;
    char*           pCommunicationBuffer;
    unsigned char   flags;
};

struct http_request
{
    const char*     pVersion;
    const char*     pMethod;
    const char*     pPath;
    const char*     pHeaders;
};

typedef http_callback_result (*http_callback_fn)(const http_request*);

enum
{
    HttpMaxFilePathLength                   = 256,
    HttpCommunicationBufferSizeInBytes      = 4096,
    HttpMaxCallbackCount                    = 32
};

enum http_server_flag : unsigned char
{
    http_server_flag_only_serve_below_root  = (1 << 0u),
    http_server_flag_manage_platform        = (1 << 1u)
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
    http_client*            pClients;
    char*                   pRootDirectory;
    char*                   pTempBuffer;
    http_request_callback   requestCallbacks[HttpMaxCallbackCount];
    size_t                  rootDirectoryLength;
    socketId                ipv4Socket;
    socketId                ipv6Socket;
    int                     activeClientCount;
    int                     port;
    int                     maxClients;
    unsigned char           flags;
};

enum http_status_code : unsigned char
{
    http_status_code_ok,
    http_status_code_not_found,
    http_status_code_bad_request
};

enum http_error_id : unsigned char
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
    void*           pMemoryBuffer;
    int             port;
    int             maxClients;
    bool            onlyServeBelowRoot; //FK: Don't allow paths like ../file.txt
    bool            allowIPV6;
    bool            handlePlatformBackend;
};

#ifdef _WIN32
bool initPlatformBackend(const http_server_parameters& parameters)
{
    if( !parameters.handlePlatformBackend )
    {
        return true;
    }

    const WORD wsaVersion = MAKEWORD(3, 0);
    WSADATA wsaData;
    const int wsaStartupResult = WSAStartup( wsaVersion, &wsaData );
    if( wsaStartupResult == 0 )
    {
        return true;
    }

    const int wsaError = WSAGetLastError();
    (void)wsaError;

    return false;
}

void shutdownPlatformBackend(http_server* pServer)
{
    if( ( pServer->flags & http_server_flag_manage_platform ) > 0)
    {
        const int wsaError = WSACleanup();
        (void)wsaError;
    }
}
#else
bool initPlatformBackend(const http_server_parameters& parameters)
{
    (void)parameters;
    return true;
}

void shutdownPlatformBackend(http_server* pServer)
{
    (void)pServer;
}
#endif

static const char* pWebSocketMagicString = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

bool isLittleEndianPlatform()
{
    union endiannes
    {
        unsigned short word;
        unsigned char bytes[2];
    };

    endiannes endiannesTest;
    endiannesTest.word = 0x1234;

    if( endiannesTest.bytes[0] == 0x12 )
    {
        return false;
    }

    return true;
}

template <typename T>
T convertToLittleEndian(T value)
{
    if( !isLittleEndianPlatform() )
    {
        return value;
    }

    unsigned char* pSwapBuffer = reinterpret_cast<unsigned char*>(&value);

    for( size_t byteIndex = 0u; byteIndex < sizeof(T) / 2; ++byteIndex )
    {
        const size_t swapIndex = sizeof(T) - byteIndex - 1;
        const unsigned char temp = pSwapBuffer[byteIndex];
        pSwapBuffer[byteIndex] = pSwapBuffer[swapIndex];
        pSwapBuffer[swapIndex] = temp; 
    }

    return value;
}

uint64_t byteCountToBitCount( size_t byteCount )
{
    return (uint64_t)(byteCount * 8);
}

bool calculateSha1Hash( uint8_t* pOutputHash, uint8_t* pInputData, size_t dataLengthInBytes )
{
    uint32_t h0 = convertToLittleEndian(0x67452301);
    uint32_t h1 = convertToLittleEndian(0xEFCDAB89);
    uint32_t h2 = convertToLittleEndian(0x98BADCFE);
    uint32_t h3 = convertToLittleEndian(0x10325476);
    uint32_t h4 = convertToLittleEndian(0xC3D2E1F0);

    uint64_t ml = byteCountToBitCount( dataLengthInBytes );
    uint8_t chunk[64]; //FK: 512 bit

    while( true )
    {
        const size_t bytesToCopy = min( dataLengthInBytes, 64 );
        if( bytesToCopy == 0u )
        {
            break;
        }
        
        memcpy(chunk, pInputData, bytesToCopy);
        pInputData += bytesToCopy;

        if( dataLenghtInBytes < 56 )
        {
            chunk[bytesToCopy] = 0x80;
            memset(&chunk[bytesToCopy+1],0,56-(bytesToCopy+1));
            memcpy(chunk[56],&ml,sizeof(ml));
        }
    }
    return false;
}

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

    //FK: TODO Find free client
    http_client* pClient = pServer->pClients + pServer->activeClientCount;
    ++pServer->activeClientCount;

    if (FD_ISSET(pServer->ipv4Socket, &readSockets))
    {
        pClient->socket = accept(pServer->ipv4Socket, NULL, NULL);
    }
    else
    {
        pClient->socket = accept(pServer->ipv6Socket, NULL, NULL);
    }

    return pClient;
}

http_error_id receiveClientData(char* pMessageBuffer, size_t* pOutMessageBufferSize, size_t messageBufferCapacityInBytes, http_client* pClient)
{
    size_t messageBufferSizeInBytes = 0u;
    while (true)
    {
        const int bytesRead = recv(pClient->socket, pClient->pCommunicationBuffer, HttpCommunicationBufferSizeInBytes, 0u);

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

        memcpy(pMessageBuffer + messageBufferSizeInBytes, pClient->pCommunicationBuffer, bytesRead);
        messageBufferSizeInBytes += bytesRead;
        *pOutMessageBufferSize = messageBufferSizeInBytes;

        if (bytesRead < HttpCommunicationBufferSizeInBytes)
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
        parse_state_end,
        parse_state_finished
    } state = parse_state_method;

    http_request request;
    request.pMethod = pMessage;

    while( *pMessage != '\0' )
    {
        if( *pMessage == ' ' || ( pMessage[ 0 ] == '\r' && pMessage[ 1 ] == '\n' ) )
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
                state = parse_state_end;
                ++pMessage;
                if( pMessage[ 0 ] == '\r' && pMessage[ 1 ] == '\n')
                {
                    request.pHeaders = ++pMessage;
                }
                else
                {
                    request.pHeaders = pMessage;
                }
            }
            else if( state == parse_state_end )
            {
                if( pMessage[ -2 ] == '\r' && pMessage[ -1 ] == '\n' )
                {
                    *pMessage = '\0';
                    state = parse_state_finished;
                    break;
                }
                else
                {
                    ++pMessage;
                }
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
    char requestBuffer[HttpCommunicationBufferSizeInBytes];
    size_t requestBufferSize = 0u;
    const http_error_id receiveResult = receiveClientData( requestBuffer, &requestBufferSize, HttpCommunicationBufferSizeInBytes, pClient );
    if( receiveResult != http_error_id_success )
    {
        return receiveResult;
    }

    return parseHttpRequest( pOutRequest, requestBuffer, requestBufferSize );
}

void destroyHttpServer(http_server* pServer)
{
    shutdownPlatformBackend( pServer );
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

size_t convertToPath( char* pTargetPath, const char* pSourcePath )
{
    size_t targetPathIndex = 0u;
    size_t sourcePathIndex = 0u;
    while( pSourcePath[ targetPathIndex ] != '\0')
    {
        if( ( targetPathIndex > 0u && pSourcePath[ targetPathIndex ] == '\\' && pSourcePath[ targetPathIndex - 1u ] == '\\' ) ||
            pSourcePath[ targetPathIndex ] == '/' )
        {
            ++targetPathIndex;

            if( pTargetPath[ sourcePathIndex ] == '/')
            {
                continue;
            }
            else
            {
                pTargetPath[ sourcePathIndex++ ] = '/';
                continue;
            }
        }

        pTargetPath[ sourcePathIndex++ ] = pSourcePath[ targetPathIndex++ ];
    }

    if( pTargetPath[ sourcePathIndex ] != '/')
    {
        pTargetPath[ sourcePathIndex++ ] = '/';
    }

    pTargetPath[ sourcePathIndex++ ] = '\0';
    return sourcePathIndex;
}

size_t calculateHttpServerMemorySizeInBytes( int maxClients )
{
    return sizeof(http_server) + sizeof(http_client) * maxClients + HttpCommunicationBufferSizeInBytes * maxClients;
}

http_server* createHttpServer(const http_server_parameters& parameters)
{
    if( !initPlatformBackend( parameters ) )
    {
        return nullptr;
    }

    if( parameters.maxClients == 0u )
    {
        return nullptr;
    }

    if( parameters.pMemoryBuffer == nullptr )
    {
        return nullptr;
    }

    char* pServerMemory = (char*)parameters.pMemoryBuffer;
    size_t serverMemorySizeInBytes = calculateHttpServerMemorySizeInBytes( parameters.maxClients );

    http_server* pServer = (http_server*)pServerMemory;
    pServerMemory += sizeof(http_server);

    if( parameters.pRootDirectory == nullptr )
    {
        pServer->pRootDirectory = "./";
    }
    else
    {
        pServer->pRootDirectory = pServerMemory;
        const size_t pathLength = convertToPath( pServer->pRootDirectory, parameters.pRootDirectory );
        pServerMemory += pathLength;
        pServer->rootDirectoryLength = pathLength;
    }

    pServer->flags              = 0u;
    pServer->activeClientCount  = 0u;
    pServer->maxClients         = parameters.maxClients;
    pServer->pClients           = (http_client*)pServerMemory;
    pServer->ipv4Socket         = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    pServer->ipv6Socket         = parameters.allowIPV6 ? socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP) : INVALID_SOCKET;
    pServer->port               = parameters.port;

    if( parameters.handlePlatformBackend )
    {
        pServer->flags |= http_server_flag_manage_platform;
    }

    if( pServer->ipv4Socket == INVALID_SOCKET && pServer->ipv6Socket == INVALID_SOCKET )
    {
        destroyHttpServer(pServer);
        return nullptr;
    }

    const char* pIPV4BindAddress = parameters.pIpv4BindAddress == nullptr ? "0.0.0.0" : parameters.pIpv4BindAddress;
    if( pServer->ipv4Socket != INVALID_SOCKET && !listenOnSocket(pServer->ipv4Socket, AF_INET, parameters.port, pIPV4BindAddress) )
    {
        destroyHttpServer(pServer);
        return nullptr;
    }

    if( parameters.allowIPV6 )
    {
        const char* pIPV6BindAddress = parameters.pIpv6BindAddress == nullptr ? "::0" : parameters.pIpv6BindAddress;
        if( pServer->ipv6Socket != INVALID_SOCKET && !listenOnSocket(pServer->ipv6Socket, AF_INET6, parameters.port, pIPV6BindAddress) )
        {
            destroyHttpServer(pServer);
            return nullptr;
        }
    }
    
    pServerMemory += sizeof(http_client) * parameters.maxClients;

    for( int clientIndex = 0u; clientIndex < parameters.maxClients; ++clientIndex )
    {
        pServer->pClients[ clientIndex ].pCommunicationBuffer = pServerMemory;
        pServer->pClients[ clientIndex ].flags                = 0u;
        pServer->pClients[ clientIndex ].socket               = INVALID_SOCKET;

        pServerMemory += HttpCommunicationBufferSizeInBytes;
    }

    pServer->pTempBuffer = pServerMemory;

    if( parameters.onlyServeBelowRoot )
    {
        pServer->flags |= http_server_flag_only_serve_below_root;
    }

    return pServer;
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

    char filePathBuffer[HttpMaxFilePathLength];
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
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "\r\n"};

            return sendToClient(pClient, message);
        }
        case http_status_code_not_found:
        {
            const char message[] = {
                "HTTP/1.1 404 Not Found\r\n"
                "\r\n" };

            return sendToClient(pClient, message);
        }
        case http_status_code_bad_request:
        {
            const char message[] = {
                "HTTP/1.1 400 Bad Request\r\n"
                "\r\n" };

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

    size_t fileOffsetInBytes = 0u;
    while (true)
    {
        const size_t bytesRead = fread(pClient->pCommunicationBuffer, 1u, HttpCommunicationBufferSizeInBytes, pFileHandle);
        if( ferror(pFileHandle) != 0 )
        {
            return http_error_id_io;
        }

        fileOffsetInBytes += bytesRead;

        const http_error_id sendResult = sendToClient(pClient, pClient->pCommunicationBuffer, bytesRead);
        if (sendResult != http_error_id_success)
        {
            return sendResult;
        }

        if (bytesRead != HttpCommunicationBufferSizeInBytes)
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

const char* findRequestHeader( const char* pRequestHeader, const char* pHeader )
{
    const char* pRunningHeader = pHeader;
    while( *pRequestHeader != '\0' )
    {
        if( *pRunningHeader == '\0' && *pRequestHeader == ' ' )
        {
            return pRequestHeader + 1;
        }

        if( *pRequestHeader == *pRunningHeader )
        {
            ++pRequestHeader;
            ++pRunningHeader;

            continue;
        }

        if( *pRequestHeader != *pRunningHeader )
        {
            pRunningHeader = pHeader;
        }

        pRequestHeader++;
    }

    return nullptr;
}

bool isStringEqualNonTerminated( const char* pStringA, const char* pStringB )
{
    while( *pStringA != '\0' && *pStringB != '\0' )
    {
        if( *pStringA != *pStringB )
        {
            return false;
        }

        ++pStringA;
        ++pStringB;
    }

    return true;
}

struct sha1
{
    unsigned int h0;
    unsigned int h1;
    unsigned int h2;
    unsigned int h3;
    unsigned int h4;

    unsigned long long ml;
    unsigned char block[64];
    unsigned char blockSize;
};

struct sha1Digest
{
    char data[20];
};

void initSha1Hash( sha1* pHash )
{
    pHash->h0 = 0x67452301;
    pHash->h1 = 0xefcdab89;
    pHash->h2 = 0x98badcfe;
    pHash->h3 = 0x10325476;
    pHash->h4 = 0xc3d2e1f0;

    pHash->ml = 0u;
    pHash->blockSize = 0u;
}

unsigned int sha1LeftRotate(unsigned int value, unsigned int bits)
{
    return ((value) << bits) | (value >> (32 - bits));
}

unsigned int changeEndianessUint32(unsigned int v)
{
    union endianessData
    {
        unsigned int v;
        unsigned char b[4];
    };

    endianessData data;
    data.v = v;
    return (unsigned int)data.b[0] >> 24u | (unsigned int)data.b[1] >> 16u | (unsigned int)data.b[2] >> 8u | (unsigned int)data.b[3] >> 0u;
}

unsigned long long changeEndianessUint64(unsigned long long v)
{
    union endianessData
    {
        unsigned long long v;
        unsigned char b[8];
    };

    endianessData data;
    data.v = v;
    return (unsigned long long)data.b[0] >> 56u | (unsigned long long)data.b[1] >> 48u | (unsigned long long)data.b[2] >> 40u | (unsigned long long)data.b[3] >> 32u |
           (unsigned long long)data.b[4] >> 24u | (unsigned long long)data.b[5] >> 16u | (unsigned long long)data.b[6] >>  8u | (unsigned long long)data.b[7] >>  0u;  
}

void hashSha1Block( sha1* pHash )
{
    unsigned int w[80];

    for( size_t i = 0u; i < 16u; ++i )
    {
        w[i] = changeEndianessUint32(w[i]);
    }

    for( size_t i = 16u; i < 80u; ++i )
    {
        w[i] = sha1LeftRotate(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    }

    unsigned int a = pHash->h0;
    unsigned int b = pHash->h1;
    unsigned int c = pHash->h2;
    unsigned int d = pHash->h3;
    unsigned int e = pHash->h4;

    unsigned int f = 0u;
    unsigned int k = 0u;
    for( size_t i = 0u; i < 80; ++i )
    {
        if( i <= 19 )
        {
            f = (b & c) | ((!b) & d);
            k = 0x5A827999;
        }
        else if( i >= 20 && i <= 39 )
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        }
        else if( i >= 40 && i <= 59 )
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        }
        else if( i >= 60 && i <= 79 )
        {
            f = b ^ c ^ d;
            k = 0xCA65C1D6;
        }

        const unsigned int temp = sha1LeftRotate(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = sha1LeftRotate(b, 30);
        b = a;
        a = temp;
    }

    pHash->h0 = pHash->h0 + a;
    pHash->h1 = pHash->h1 + b;
    pHash->h2 = pHash->h2 + c;
    pHash->h3 = pHash->h3 + d;
    pHash->h4 = pHash->h4 + e;
}

void addSha1Hash( sha1* pHash, const void* pData, size_t dataSizeInBytes )
{
    if( pHash->blockSize + dataSizeInBytes > sizeof( pHash->block ) )
    {
        const size_t dataSizeToCopy = dataSizeInBytes - sizeof( pHash->block );
        memcpy( pHash->block + pHash->blockSize, pData, dataSizeToCopy );
        hashSha1Block( pHash );
        dataSizeInBytes -= dataSizeToCopy;
    }
}

sha1Digest finishSha1Hash( sha1* pHash )
{
    const unsigned char firstBit = 0x80;
    addSha1Hash(pHash, &firstBit, 1u);
    
    while( pHash->blockSize != 56u )
    {
        const unsigned char zero = 0x0u;
        addSha1Hash(pHash, &zero, 1u);
    }

    const unsigned long long messageLength = changeEndianessUint64(pHash->ml);
    addSha1Hash(pHash,&messageLength,8u);

    assert(pHash->blockSize == 0u);
    
    unsigned int digestData[5];

    digestData[0] = sha1LeftRotate(pHash->h0, 128);
    digestData[1] = sha1LeftRotate(pHash->h1, 96);
    digestData[2] = sha1LeftRotate(pHash->h2, 64);
    digestData[3] = sha1LeftRotate(pHash->h3, 32);
    digestData[4] = pHash->h4;

    sha1Digest digest;
    memcpy(&digest, digestData, sizeof(digestData));

    return digest;
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
            sendHttpStatusCodeToClient(pClient, http_status_code_bad_request);
            closeClientConnection(pClient);
            continue;
        }

        //FK: Debug:
        printf(clientRequest.pMethod);
        printf(clientRequest.pPath);
        printf(clientRequest.pVersion);
        printf(clientRequest.pHeaders);
        printf("\n");

        const char* pConnectionType = findRequestHeader( clientRequest.pHeaders, "Connection:");
        if( isStringEqualNonTerminated(pConnectionType, "keep-alive") )
        {
            pClient->flags |= http_client_flag_keep_alive;
        }
        else if( isStringEqualNonTerminated(pConnectionType, "Upgrade") )
        {
            const char* pConnectionUpgradeType = findRequestHeader( clientRequest.pHeaders, "Upgrade:");
            if( isStringEqualNonTerminated(pConnectionUpgradeType, "websocket") )
            {
                const char* pWebsocketKey = findRequestHeader( clientRequest.pHeaders, "Sec-WebSocket-Key:" );
                if( pWebsocketKey == nullptr )
                {
                    sendHttpStatusCodeToClient(pClient, http_status_code_bad_request);
                    closeClientConnection(pClient);
                    continue;
                }

                sha1 websocketHash;
                initSha1Hash( &websocketHash );
                addSha1Hash( &websocketHash, pWebsocketKey );
                addSha1Hash( &websocketHash, "" );

                pClient->flags |= http_client_flag_websocket;
            }
        }

#if 0
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
#endif

        if( strcmp( clientRequest.pMethod, "GET" ) == 0 )
        {
            FILE* pFileHandle = nullptr;

            char pathBuffer[HttpMaxFilePathLength];
            pathBuffer[0] = '\0';

            strcat( pathBuffer, pServer->pRootDirectory );
            strcat( pathBuffer, clientRequest.pPath );
            if( isDirectoryPath( clientRequest.pPath ) )
            {
                //FK: Look for index.html
                strcat( pathBuffer, "index.html" );

                pFileHandle = fopen( pathBuffer, "rb" );
                if( pFileHandle == nullptr )
                {
                    //FK: Try index.htm
                    pathBuffer[ strlen(pathBuffer) - 1u ] = 0u;
                    pFileHandle = fopen( pathBuffer, "rb" );

                    if( pFileHandle == nullptr )
                    {
                        sendHttpStatusCodeToClient(pClient, http_status_code_not_found);
                    }
                }
            }
            else
            {
                pFileHandle = fopen(pathBuffer, "rb");
            }

            if( pFileHandle == nullptr )
            {
                sendHttpStatusCodeToClient(pClient, http_status_code_not_found);
            }
            else
            {
                sendHttpStatusCodeToClient(pClient, http_status_code_ok);
                sendFileContentToClient(pClient, pFileHandle);
                fclose(pFileHandle);
            }

            if( ( pClient->flags & http_client_flag_keep_alive ) == 0u )
            {
                closeClientConnection(pClient);
            }
        }
    }
}

#endif //K15_HTTP_SERVER_INCLUDE