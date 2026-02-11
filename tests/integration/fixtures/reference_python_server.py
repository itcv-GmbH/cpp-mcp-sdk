#!/usr/bin/env python3

import argparse
from typing import Optional

from mcp.server.auth.provider import AccessToken, TokenVerifier
from mcp.server.auth.settings import AuthSettings
from mcp.server.fastmcp import FastMCP


class StaticTokenVerifier(TokenVerifier):
    def __init__(self, expected_token: str) -> None:
        self._expected_token = expected_token

    async def verify_token(self, token: str) -> Optional[AccessToken]:
        if token != self._expected_token:
            return None

        return AccessToken(
            token=token,
            client_id="python-reference-client",
            scopes=["mcp:read"],
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Reference Python MCP server fixture")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--path", default="/mcp")
    parser.add_argument("--token", default="integration-token")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    server = FastMCP(
        "python-reference-server",
        host=args.host,
        port=args.port,
        streamable_http_path=args.path,
        json_response=True,
        stateless_http=True,
        log_level="WARNING",
        token_verifier=StaticTokenVerifier(args.token),
        auth=AuthSettings(
            issuer_url="https://auth.integration.example",
            resource_server_url=f"http://{args.host}:{args.port}",
            required_scopes=["mcp:read"],
        ),
    )

    @server.tool(name="python_echo", description="Echo text provided by the caller")
    def python_echo(text: str) -> str:
        return f"python echo: {text}"

    @server.resource(
        "resource://python-server/info",
        name="python-server-info",
        description="Reference server metadata",
    )
    def server_info() -> str:
        return "python reference server resource"

    @server.prompt(
        name="python_server_prompt",
        description="Returns a prompt containing the supplied topic",
    )
    def python_server_prompt(topic: str) -> str:
        return f"Python reference prompt topic: {topic}"

    print(
        f"reference_python_server endpoint=http://{args.host}:{args.port}{args.path}",
        flush=True,
    )
    server.run(transport="streamable-http")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
