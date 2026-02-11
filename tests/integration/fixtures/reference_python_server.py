#!/usr/bin/env python3

import argparse
import asyncio
from typing import Any, Optional

from mcp.server.auth.provider import AccessToken, TokenVerifier
from mcp.server.auth.settings import AuthSettings
from mcp.server.fastmcp import Context, FastMCP
from mcp.types import SamplingMessage, TextContent
from pydantic import BaseModel, Field


EXPECTED_CPP_CLIENT_SAMPLING_RESPONSE = "cpp-client-sampling-response"
EXPECTED_CPP_CLIENT_ELICITATION_REASON = "cpp-client-approved"


class IntegrationElicitationData(BaseModel):
    approved: bool = Field(description="Whether the C++ client approved")
    reason: str = Field(description="Approval reason from C++ client")


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
        json_response=False,
        stateless_http=False,
        log_level="WARNING",
        token_verifier=StaticTokenVerifier(args.token),
        auth=AuthSettings(
            issuer_url="https://auth.integration.example",
            resource_server_url=f"http://{args.host}:{args.port}",
            required_scopes=["mcp:read"],
        ),
    )

    sampling_verification_task: asyncio.Task[None] | None = None
    elicitation_verification_task: asyncio.Task[None] | None = None
    outbound_verifications_completed = False

    async def verify_sampling_round_trip(session: Any, related_request_id: str) -> None:
        sampling_result = await asyncio.wait_for(
            session.create_message(
                messages=[
                    SamplingMessage(
                        role="user",
                        content=TextContent(
                            type="text", text="python-server-sampling-check"
                        ),
                    )
                ],
                max_tokens=64,
                related_request_id=related_request_id,
            ),
            timeout=10.0,
        )

        if getattr(sampling_result.content, "type", None) != "text":
            raise RuntimeError(
                f"C++ client sampling response was not text content: {sampling_result.content!r}"
            )

        if sampling_result.content.text != EXPECTED_CPP_CLIENT_SAMPLING_RESPONSE:
            raise RuntimeError(
                "C++ client sampling response text mismatch. "
                f"expected={EXPECTED_CPP_CLIENT_SAMPLING_RESPONSE!r} "
                f"actual={sampling_result.content.text!r}"
            )

    async def verify_elicitation_round_trip(
        session: Any, related_request_id: str
    ) -> None:
        elicitation_result = await asyncio.wait_for(
            session.elicit(
                message="python-server-elicitation-check",
                requestedSchema=IntegrationElicitationData.model_json_schema(),
                related_request_id=related_request_id,
            ),
            timeout=10.0,
        )

        if elicitation_result.action != "accept" or elicitation_result.content is None:
            raise RuntimeError(
                "C++ client elicitation response was not accepted with data. "
                f"result={elicitation_result!r}"
            )

        approved = elicitation_result.content.get("approved")
        reason = elicitation_result.content.get("reason")

        if approved is not True or reason != EXPECTED_CPP_CLIENT_ELICITATION_REASON:
            raise RuntimeError(
                "C++ client elicitation content mismatch. "
                f"approved={approved!r} "
                f"reason={reason!r}"
            )

    async def assert_outbound_verifications() -> None:
        nonlocal outbound_verifications_completed
        if outbound_verifications_completed:
            return

        if sampling_verification_task is None or elicitation_verification_task is None:
            raise RuntimeError(
                "Reference server did not initiate sampling/elicitation verification tasks"
            )

        try:
            await asyncio.wait_for(
                asyncio.gather(
                    sampling_verification_task, elicitation_verification_task
                ),
                timeout=10.0,
            )
        except Exception as error:
            raise RuntimeError(
                "Reference server outbound sampling/elicitation verification failed: "
                f"{type(error).__name__}: {error}"
            ) from error

        outbound_verifications_completed = True

    @server.tool(name="python_echo", description="Echo text provided by the caller")
    async def python_echo(text: str, ctx: Context) -> str:
        nonlocal sampling_verification_task
        nonlocal elicitation_verification_task
        session = ctx.session
        related_request_id = ctx.request_id

        if sampling_verification_task is None:
            sampling_verification_task = asyncio.create_task(
                verify_sampling_round_trip(session, related_request_id)
            )

        if elicitation_verification_task is None:
            elicitation_verification_task = asyncio.create_task(
                verify_elicitation_round_trip(session, related_request_id)
            )

        await asyncio.sleep(0)

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
    async def python_server_prompt(topic: str) -> str:
        await assert_outbound_verifications()
        return f"Python reference prompt topic: {topic}"

    print(
        f"reference_python_server endpoint=http://{args.host}:{args.port}{args.path}",
        flush=True,
    )
    server.run(transport="streamable-http")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
