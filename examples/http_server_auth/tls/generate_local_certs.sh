#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${SCRIPT_DIR}"

CA_KEY="${OUTPUT_DIR}/ca-key.pem"
CA_CERT="${OUTPUT_DIR}/ca-cert.pem"
SERVER_KEY="${OUTPUT_DIR}/localhost-key.pem"
SERVER_CSR="${OUTPUT_DIR}/localhost.csr"
SERVER_CERT="${OUTPUT_DIR}/localhost-cert.pem"
OPENSSL_CONFIG="${OUTPUT_DIR}/openssl-localhost.cnf"

if [[ ! -f "${OPENSSL_CONFIG}" ]]; then
  printf 'Missing OpenSSL config: %s\n' "${OPENSSL_CONFIG}" >&2
  exit 1
fi

openssl genrsa -out "${CA_KEY}" 2048
openssl req -x509 -new -nodes -key "${CA_KEY}" -sha256 -days 3650 -subj "/CN=mcp-local-ca/O=MCP SDK Examples" -out "${CA_CERT}"

openssl genrsa -out "${SERVER_KEY}" 2048
openssl req -new -key "${SERVER_KEY}" -out "${SERVER_CSR}" -config "${OPENSSL_CONFIG}"

openssl x509 -req -in "${SERVER_CSR}" -CA "${CA_CERT}" -CAkey "${CA_KEY}" -CAcreateserial -out "${SERVER_CERT}" -days 825 -sha256 -extensions v3_req -extfile "${OPENSSL_CONFIG}"

rm -f "${SERVER_CSR}" "${OUTPUT_DIR}/ca-cert.srl"

printf 'Generated:\n'
printf '  %s\n' "${CA_CERT}"
printf '  %s\n' "${SERVER_CERT}"
printf '  %s\n' "${SERVER_KEY}"
