# SPDX-License-Identifier: MPL-2.0
"""Software test phone for the real host worker. Private keys are temporary."""
import hashlib
import http.client
from pathlib import Path
import subprocess

def openssl(*args, data=None):
    result = subprocess.run(['openssl', *map(str, args)], input=data, capture_output=True, check=True)
    return result.stdout

class SoftwarePhone:
    def __init__(self, directory, name='Test phone'):
        self.key = Path(directory) / (name.replace(' ', '-') + '.pem')
        openssl('genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-out', self.key)
        self.key.chmod(0o600)
        self.public = openssl('pkey', '-in', self.key, '-pubout', '-outform', 'DER')[-65:]
        assert self.public[0] == 4
        self.id = hashlib.sha256(self.public).hexdigest()
        self.name = name
        self.nonces = {}

    @staticmethod
    def http(host, method, path, body=b'', headers=None):
        client = http.client.HTTPConnection(host, timeout=5)
        try:
            client.request(method, path, body=body, headers=headers or {})
            response = client.getresponse()
            return response.status, response.read(), dict(response.getheaders())
        finally:
            client.close()

    def request(self, host, method, path, body=b'', *, signed_body=None, nonce=None, bad_signature=False):
        if nonce is None:
            nonce = self.nonces.pop(host, None)
        if nonce is None:
            status, _, headers = self.http(host, 'GET', '/v1/nonce')
            assert status == 200, status
            nonce = headers['X-NBT-Nonce']
        digest = hashlib.sha256(body if signed_body is None else signed_body).hexdigest()
        canonical = f'NBT-REQ-1\n{method}\n{path}\n{host}\n{nonce}\n{len(body)}\n{digest}\n'.encode()
        signature = bytearray(openssl('dgst', '-sha256', '-sign', self.key, data=canonical))
        if bad_signature:
            signature[-1] ^= 1
        headers = {'X-NBT-Key': self.id, 'X-NBT-Nonce': nonce, 'X-NBT-Signature': signature.hex()}
        if method == 'POST':
            streamed = path in ('/v1/upload', '/v1/files/data')
            headers['Content-Type'] = 'application/octet-stream' if streamed else 'text/plain'
        status, data, response = self.http(host, method, path, body, headers)
        if 'X-NBT-Nonce' in response:
            self.nonces[host] = response['X-NBT-Nonce']
        self.last_nonce = nonce
        return status, data

    def pair_request(self, host):
        return self.request(host, 'POST', '/v1/pair',
                            f'NBTPAIR1\nname={self.name}\nkey={self.public.hex()}\n'.encode())

    def pair(self, command, host):
        assert command('PHONE OPEN') == 'OK'
        status, data = self.pair_request(host)
        assert status == 202, (status, data)
        expected = int.from_bytes(hashlib.sha256(bytes.fromhex(self.last_nonce)+self.public).digest()[:4], 'big') % 1000000
        assert data == f'PAIR\tpending\t{expected:06d}\n'.encode()
        pending = command('PHONE STATUS').split('\t')
        assert pending[0:2] == ['PHONE', 'pair'] and pending[3] == self.name
        assert command('PHONE YES '+pending[2]) == 'OK paired'
        status, data = self.request(host, 'GET', '/v1/status')
        assert status == 200 and data.startswith(b'STATUS\tpaired\t'), (status, data)
