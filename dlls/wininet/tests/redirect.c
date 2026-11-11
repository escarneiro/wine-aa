/*
 * Tests for redirects of POST requests
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "winsock2.h"
#include "wininet.h"

#include "wine/test.h"

static int http_port, tls_port;
static char target_verb[16];
static int target_body_len;
static LONG tls_connections;

static SOCKET listen_socket(int *port)
{
    struct sockaddr_in addr = { 0 };
    int len = sizeof(addr);
    SOCKET s;

    s = socket(AF_INET, SOCK_STREAM, 0);
    ok(s != INVALID_SOCKET, "socket failed, error %d.\n", WSAGetLastError());
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(!bind(s, (struct sockaddr *)&addr, sizeof(addr)), "bind failed, error %d.\n", WSAGetLastError());
    ok(!listen(s, 5), "listen failed, error %d.\n", WSAGetLastError());
    getsockname(s, (struct sockaddr *)&addr, &len);
    *port = ntohs(addr.sin_port);
    return s;
}

/* Reads one request; returns the header length, or -1. */
static int read_request(SOCKET c, char *buf, int size, int *body_len)
{
    int len = 0, ret, header_len, content_len = 0;
    char *end, *cl;

    for (;;)
    {
        buf[len] = 0;
        if ((end = strstr(buf, "\r\n\r\n"))) break;
        if (len >= size - 1) return -1;
        if ((ret = recv(c, buf + len, size - 1 - len, 0)) <= 0) return -1;
        len += ret;
    }
    header_len = end + 4 - buf;

    if ((cl = strstr(buf, "Content-Length: ")) && cl < end)
        content_len = atoi(cl + 16);
    *body_len = len - header_len;
    while (*body_len < content_len)
    {
        char tmp[1024];
        if ((ret = recv(c, tmp, sizeof(tmp), 0)) <= 0) break;
        *body_len += ret;
    }
    return header_len;
}

static DWORD CALLBACK http_server(void *arg)
{
    SOCKET s = (SOCKET)arg, c;
    char buf[4096], response[512];
    int body_len, code;

    while ((c = accept(s, NULL, NULL)) != INVALID_SOCKET)
    {
        if (read_request(c, buf, sizeof(buf), &body_len) > 0)
        {
            if (sscanf(buf, "%*s /redirect/https/%d", &code) == 1)
                sprintf(response, "HTTP/1.1 %d Redirect\r\nLocation: https://127.0.0.1:%d/target\r\n"
                        "Content-Length: 0\r\nConnection: close\r\n\r\n", code, tls_port);
            else if (sscanf(buf, "%*s /redirect/http/%d", &code) == 1)
                sprintf(response, "HTTP/1.1 %d Redirect\r\nLocation: http://127.0.0.1:%d/target\r\n"
                        "Content-Length: 0\r\nConnection: close\r\n\r\n", code, http_port);
            else
            {
                sscanf(buf, "%15s", target_verb);
                target_body_len = body_len;
                strcpy(response, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
            }
            send(c, response, strlen(response), 0);
        }
        closesocket(c);
    }
    return 0;
}

static DWORD CALLBACK tls_server(void *arg)
{
    SOCKET s = (SOCKET)arg, c;

    while ((c = accept(s, NULL, NULL)) != INVALID_SOCKET)
    {
        InterlockedIncrement(&tls_connections);
        closesocket(c);
    }
    return 0;
}

static void test_post_redirect(const char *path, DWORD flags)
{
    static char body[] = "<request>some data</request>";
    static const char headers[] = "Content-Type: text/xml\r\n";
    HINTERNET session, connection, request;
    DWORD status = 0, size = sizeof(status), error, timeout = 5000;
    char location[256] = "";
    BOOL ret;

    target_verb[0] = 0;
    target_body_len = -1;
    tls_connections = 0;

    session = InternetOpenA("winetest", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    ok(session != NULL, "InternetOpen failed, error %lu.\n", GetLastError());
    InternetSetOptionA(session, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(session, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(session, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    connection = InternetConnectA(session, "127.0.0.1", http_port, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    ok(connection != NULL, "InternetConnect failed, error %lu.\n", GetLastError());
    request = HttpOpenRequestA(connection, "POST", path, NULL, NULL, NULL,
                               INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_UI | INTERNET_FLAG_RELOAD
                               | INTERNET_FLAG_NO_CACHE_WRITE | flags, 0);
    ok(request != NULL, "HttpOpenRequest failed, error %lu.\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = HttpSendRequestA(request, headers, -1, body, sizeof(body) - 1);
    error = ret ? 0 : GetLastError();
    HttpQueryInfoA(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &size, NULL);
    size = sizeof(location);
    HttpQueryInfoA(request, HTTP_QUERY_LOCATION, location, &size, NULL);
    Sleep(200);

    trace("RESULT %s flags %#lx: ret %d error %lu status %lu location \"%s\" "
          "target verb \"%s\" body %d tls connections %ld\n",
          path, flags, ret, error, status, location, target_verb, target_body_len, tls_connections);

    InternetCloseHandle(request);
    InternetCloseHandle(connection);
    InternetCloseHandle(session);
}

START_TEST(redirect)
{
    SOCKET http_socket, tls_socket;
    WSADATA wsa;

    WSAStartup(MAKEWORD(2, 2), &wsa);
    http_socket = listen_socket(&http_port);
    tls_socket = listen_socket(&tls_port);
    CloseHandle(CreateThread(NULL, 0, http_server, (void *)http_socket, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, tls_server, (void *)tls_socket, 0, NULL));

    test_post_redirect("/redirect/https/301", 0);
    test_post_redirect("/redirect/https/302", 0);
    test_post_redirect("/redirect/https/307", 0);
    test_post_redirect("/redirect/https/301", INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS);
    test_post_redirect("/redirect/http/301", 0);
    test_post_redirect("/redirect/http/302", 0);
    test_post_redirect("/redirect/http/303", 0);
    test_post_redirect("/redirect/http/307", 0);

    closesocket(http_socket);
    closesocket(tls_socket);
    WSACleanup();
}
