param(
    [string]$OutputDir = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'

$CA_KEY = Join-Path $OutputDir 'tls-ca-key.pem'
$CA_CERT = Join-Path $OutputDir 'tls-ca-cert.pem'
$SERVER_KEY = Join-Path $OutputDir 'tls-localhost-key.pem'
$SERVER_CSR = Join-Path $OutputDir 'tls-localhost.csr'
$SERVER_CERT = Join-Path $OutputDir 'tls-localhost-cert.pem'

# Find openssl in PATH
$openssl = Get-Command openssl -ErrorAction Stop

# Generate CA private key and certificate
& $openssl.Source genrsa -out $CA_KEY 2048 2>$null
& $openssl.Source req -x509 -new -nodes -key $CA_KEY -sha256 -days 3650 `
  -subj '/CN=mcp-test-ca/O=MCP SDK Tests' `
  -out $CA_CERT

# Generate server private key and certificate
& $openssl.Source genrsa -out $SERVER_KEY 2048 2>$null
& $openssl.Source req -new -key $SERVER_KEY -out $SERVER_CSR `
  -subj '/CN=localhost/O=MCP SDK Tests'

# Create SAN config file
$SAN_CONFIG = Join-Path $OutputDir 'san.cnf'
@'
subjectAltName=DNS:localhost,IP:127.0.0.1
'@ | Set-Content -Path $SAN_CONFIG -NoNewline

# Sign server certificate with CA
& $openssl.Source x509 -req -in $SERVER_CSR -CA $CA_CERT -CAkey $CA_KEY `
  -CAcreateserial -out $SERVER_CERT -days 825 -sha256 `
  -extfile $SAN_CONFIG

# Clean up intermediate files
Remove-Item -Path $SERVER_CSR -ErrorAction SilentlyContinue
Remove-Item -Path (Join-Path $OutputDir 'tls-ca-cert.srl') -ErrorAction SilentlyContinue
Remove-Item -Path $SAN_CONFIG -ErrorAction SilentlyContinue

Write-Host 'Generated test certificates:'
Write-Host "  $CA_CERT"
Write-Host "  $SERVER_CERT"
Write-Host "  $SERVER_KEY"
