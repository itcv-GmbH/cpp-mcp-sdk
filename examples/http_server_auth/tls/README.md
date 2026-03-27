# TLS Certificate Generation

This directory contains utility scripts and configurations for generating self-signed TLS certificates. These certificates are required for testing the `http_server_auth` example, as the MCP SDK enforces that OAuth/Bearer tokens must be transmitted over encrypted (HTTPS) connections.

## Files

- `generate_local_certs.sh`: A bash script that uses `openssl` to generate a local Certificate Authority (CA) and a server certificate signed by that CA.
- `openssl-localhost.cnf`: The OpenSSL configuration file defining the Subject Alternative Names (SANs) for `localhost` and `127.0.0.1` to ensure browsers and client libraries trust the certificate during local testing.

## Usage

To generate the certificates, simply execute the script from the terminal:

```bash
chmod +x generate_local_certs.sh
./generate_local_certs.sh
```

## Generated Artifacts

Executing the script will generate the following artifacts in this directory:
- `ca-key.pem`: The private key for the local Certificate Authority.
- `ca-cert.pem`: The public certificate for the local Certificate Authority.
- `localhost-key.pem`: The private key for the MCP HTTP server.
- `localhost-cert.pem`: The public certificate for the MCP HTTP server (signed by `ca-cert.pem`).

> **Note:** These certificates are exclusively for local testing. In a production environment, you should use certificates issued by a trusted public Certificate Authority (e.g., Let's Encrypt).