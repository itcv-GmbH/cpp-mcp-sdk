#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${SCRIPT_DIR}"

CA_KEY="${OUTPUT_DIR}/tls-ca-key.pem"
CA_CERT="${OUTPUT_DIR}/tls-ca-cert.pem"
SERVER_KEY="${OUTPUT_DIR}/tls-localhost-key.pem"
SERVER_CSR="${OUTPUT_DIR}/tls-localhost.csr"
SERVER_CERT="${OUTPUT_DIR}/tls-localhost-cert.pem"

# Generate CA private key and certificate
openssl genrsa -out "${CA_KEY}" 2048 2>/dev/null
openssl req -x509 -new -nodes -key "${CA_KEY}" -sha256 -days 3650 \
  -subj "/CN=mcp-test-ca/O=MCP SDK Tests" \
  -out "${CA_CERT}"

# Generate server private key and certificate
openssl genrsa -out "${SERVER_KEY}" 2048 2>/dev/null
openssl req -new -key "${SERVER_KEY}" -out "${SERVER_CSR}" \
  -subj "/CN=localhost/O=MCP SDK Tests"

# Sign server certificate with CA
openssl x509 -req -in "${SERVER_CSR}" -CA "${CA_CERT}" -CAkey "${CA_KEY}" \
  -CAcreateserial -out "${SERVER_CERT}" -days 825 -sha256 \
  -extfile <(printf "subjectAltName=DNS:localhost,IP:127.0.0.1")

# Clean up intermediate files
rm -f "${SERVER_CSR}" "${OUTPUT_DIR}/tls-ca-cert.srl"

printf 'Generated test certificates:\n'
printf '  %s\n' "${CA_CERT}"
printf '  %s\n' "${SERVER_CERT}"
printf '  %s\n' "${SERVER_KEY}"
