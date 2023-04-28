/*
  SSLClient.cpp - Base class that provides Client SSL to ESP32
  Additions (c) 2011 Adrian McEwen.  All right reserved.
  Additions Copyright (C) 2017 Evandro Luis Copercini.
  Additions Copyright (C) 2019 Vadim Govorovski.
  Additions Copyright (C) 2023 Maximiliano Ramirez.
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "SSLClient.h"
#include "esp_crt_bundle.h"
#include <errno.h>

#undef connect
#undef write
#undef read


SSLClient::SSLClient()
{
    _connected = false;

    sslclient = new SSLClientLib::sslclient_context;
    SSLClientLib::ssl_init(sslclient, nullptr);
    sslclient->handshake_timeout = 120000;
    _use_insecure = false;
    _CA_cert = NULL;
    _cert = NULL;
    _private_key = NULL;
    _pskIdent = NULL;
    _psKey = NULL;
    _alpn_protos = NULL;
    _use_ca_bundle = false;
}

SSLClient::SSLClient(Client* client)
{
    _connected = false;

    sslclient = new SSLClientLib::sslclient_context;
    SSLClientLib::ssl_init(sslclient, client);
    sslclient->handshake_timeout = 120000;
    _CA_cert = NULL;
    _cert = NULL;
    _private_key = NULL;
    _pskIdent = NULL;
    _psKey = NULL;
    _alpn_protos = NULL;
    _use_ca_bundle = false;
}

SSLClient::~SSLClient()
{
    stop();
    delete sslclient;
}

void SSLClient::stop()
{
    if (sslclient->client >= 0) {
        //sslclient->client->stop();
        _connected = false;
        _peek = -1;
    }
    SSLClientLib::stop_ssl_socket(sslclient, _CA_cert, _cert, _private_key);
}

int SSLClient::connect(IPAddress ip, uint16_t port)
{
    if (_pskIdent && _psKey)
        return connect(ip, port, _pskIdent, _psKey);
    return connect(ip, port, _CA_cert, _cert, _private_key);
}

int SSLClient::connect(IPAddress ip, uint16_t port, int32_t timeout){
    _timeout = timeout;
    return connect(ip, port);
}

int SSLClient::connect(const char *host, uint16_t port)
{
    if (_pskIdent && _psKey)
        return connect(host, port, _pskIdent, _psKey);
    return connect(host, port, _CA_cert, _cert, _private_key);
}

int SSLClient::connect(const char *host, uint16_t port, int32_t timeout){
    _timeout = timeout;
    return connect(host, port);
}

int SSLClient::connect(IPAddress ip, uint16_t port, const char *CA_cert, const char *cert, const char *private_key)
{
    return connect(ip.toString().c_str(), port, CA_cert, cert, private_key);
}

int SSLClient::connect(const char *host, uint16_t port, const char *CA_cert, const char *cert, const char *private_key)
{
    log_d("Connecting to %s:%d", host, port);
    if(_timeout > 0){
        sslclient->handshake_timeout = _timeout;
    }
    int ret = SSLClientLib::start_ssl_client(sslclient, host, port, _timeout, CA_cert, _use_ca_bundle, cert, private_key, NULL, NULL, _use_insecure, _alpn_protos);
    _lastError = ret;
    if (ret < 0) {
        log_e("start_ssl_client: %d", ret);
        stop();
        _connected = false;
        return 0;
    }
    log_i("SSL connection established");
    _connected = true;
    return 1;
}

int SSLClient::connect(IPAddress ip, uint16_t port, const char *pskIdent, const char *psKey) {
    return connect(ip.toString().c_str(), port, pskIdent, psKey);
}

int SSLClient::connect(const char *host, uint16_t port, const char *pskIdent, const char *psKey) {
    log_v("start_ssl_client with PSK");
    if(_timeout > 0){
        sslclient->handshake_timeout = _timeout;
    }
    int ret = SSLClientLib::start_ssl_client(sslclient, host, port, _timeout, NULL, false, NULL, NULL, _pskIdent, _psKey, _use_insecure, _alpn_protos);
    _lastError = ret;
    if (ret < 0) {
        log_e("start_ssl_client: %d", ret);
        stop();
        return 0;
    }
    log_i("SSL connection established");
    _connected = true;
    return 1;
}

int SSLClient::peek(){
    if(_peek >= 0){
        return _peek;
    }
    _peek = timedRead();
    return _peek;
}

size_t SSLClient::write(uint8_t data)
{
    return write(&data, 1);
}

int SSLClient::read()
{
    uint8_t data = -1;
    int res = read(&data, 1);
    if (res < 0) {
        return res;
    }
    return data;
}

size_t SSLClient::write(const uint8_t *buf, size_t size)
{
    if (!_connected) {
        return 0;
    }
    int res = SSLClientLib::send_ssl_data(sslclient, buf, size);
    if (res < 0) {
        stop();
        res = 0;
    }
    return res;
}

int SSLClient::read(uint8_t *buf, size_t size)
{
    int peeked = 0;
    int avail = available();
    if ((!buf && size) || avail <= 0) {
        return -1;
    }
    if(!size){
        return 0;
    }
    if(_peek >= 0){
        buf[0] = _peek;
        _peek = -1;
        size--;
        avail--;
        if(!size || !avail){
            return 1;
        }
        buf++;
        peeked = 1;
    }
    
    int res = SSLClientLib::get_ssl_receive(sslclient, buf, size);
    if (res < 0) {
        stop();
        return peeked?peeked:res;
    }
    return res + peeked;
}

int SSLClient::available()
{
    int peeked = (_peek >= 0);
    if (!_connected) {
        return peeked;
    }
    int res = SSLClientLib::data_to_read(sslclient);
    if (res < 0) {
        stop();
        return peeked?peeked:res;
    }
    return res+peeked;
}

uint8_t SSLClient::connected()
{
    uint8_t dummy = 0;
    read(&dummy, 0);

    return _connected;
}

void SSLClient::setInsecure()
{
    _CA_cert = NULL;
    _cert = NULL;
    _private_key = NULL;
    _pskIdent = NULL;
    _psKey = NULL;
    _use_insecure = true;   
}

void SSLClient::setCACert (const char *rootCA)
{
    _CA_cert = rootCA;
}

void SSLClient::setCACertBundle(const uint8_t * bundle)
{
    if (bundle != NULL)
    {
        arduino_esp_crt_bundle_set(bundle);
        _use_ca_bundle = true;
    } else {
        arduino_esp_crt_bundle_detach(NULL);
        _use_ca_bundle = false;
    }
}

void SSLClient::setCertificate (const char *client_ca)
{
    _cert = client_ca;
}

void SSLClient::setPrivateKey (const char *private_key)
{
    _private_key = private_key;
}

void SSLClient::setPreSharedKey(const char *pskIdent, const char *psKey) {
    _pskIdent = pskIdent;
    _psKey = psKey;
}

bool SSLClient::verify(const char* fp, const char* domain_name)
{
    if (!sslclient)
        return false;

    return SSLClientLib::verify_ssl_fingerprint(sslclient, fp, domain_name);
}

char *SSLClient::_streamLoad(Stream& stream, size_t size) {
  char *dest = (char*)malloc(size+1);
  if (!dest) {
    return nullptr;
  }
  if (size != stream.readBytes(dest, size)) {
    free(dest);
    dest = nullptr;
    return nullptr;
  }
  dest[size] = '\0';
  return dest;
}

bool SSLClient::loadCACert(Stream& stream, size_t size) {
  if (_CA_cert != NULL) free(const_cast<char*>(_CA_cert));
  char *dest = _streamLoad(stream, size);
  bool ret = false;
  if (dest) {
    setCACert(dest);
    ret = true;
  }
  return ret;
}

bool SSLClient::loadCertificate(Stream& stream, size_t size) {
  if (_cert != NULL) free(const_cast<char*>(_cert));
  char *dest = _streamLoad(stream, size);
  bool ret = false;
  if (dest) {
    setCertificate(dest);
    ret = true;
  }
  return ret;
}

bool SSLClient::loadPrivateKey(Stream& stream, size_t size) {
  if (_private_key != NULL) free(const_cast<char*>(_private_key));
  char *dest = _streamLoad(stream, size);
  bool ret = false;
  if (dest) {
    setPrivateKey(dest);
    ret = true;
  }
  return ret;
}

int SSLClient::lastError(char *buf, const size_t size)
{
    if (!_lastError) {
        return 0;
    }
    mbedtls_strerror(_lastError, buf, size);
    return _lastError;
}

void SSLClient::setHandshakeTimeout(unsigned long handshake_timeout)
{
    sslclient->handshake_timeout = handshake_timeout * 1000;
}

void SSLClient::setAlpnProtocols(const char **alpn_protos)
{
    _alpn_protos = alpn_protos;
}