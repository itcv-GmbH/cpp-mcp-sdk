#!/usr/bin/env python3

import argparse
import asyncio
import logging
from typing import Any, Dict, Optional, Set

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

    # Track current log level
    current_log_level = logging.INFO

    # Task storage for tasks/cancel support
    tasks: dict[str, dict] = {}

    # Track resource subscriptions
    resource_subscriptions: Dict[str, Set[str]] = {}  # uri -> set of session_ids

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

    @server.tool(name="ping", description="Ping the server to check connectivity")
    async def ping() -> str:
        return "pong"

    @server.tool(
        name="logging_setLevel",
        description="Set the logging level and emit a notification",
    )
    async def logging_setLevel(level: str, ctx: Context) -> str:
        nonlocal current_log_level
        session = ctx.session

        level_map = {
            "debug": logging.DEBUG,
            "info": logging.INFO,
            "warning": logging.WARNING,
            "error": logging.ERROR,
        }
        current_log_level = level_map.get(level.lower(), logging.INFO)

        # Emit notification
        await session.send_notification(
            {
                "jsonrpc": "2.0",
                "method": "notifications/message",
                "params": {
                    "level": level,
                    "logger": "reference_python_server",
                    "data": f"Log level set to {level}",
                },
            }
        )
        return f"Log level set to {level}"

    @server.tool(
        name="completion_complete",
        description="Return completion suggestions for an argument",
    )
    async def completion_complete(ref: dict, argument: dict) -> dict:
        # Return completion suggestions
        return {
            "completion": {
                "values": ["option1", "option2", "option3"],
                "hasMore": False,
            }
        }

    @server.tool(
        name="tasks_create",
        description="Create a new background task",
    )
    async def tasks_create(tool: str, arguments: dict, ctx: Context) -> dict:
        nonlocal tasks
        session = ctx.session

        task_id = f"task-{len(tasks) + 1}"
        tasks[task_id] = {
            "id": task_id,
            "status": "working",
            "tool": tool,
            "arguments": arguments,
        }

        # Emit status notification
        await session.send_notification(
            {
                "jsonrpc": "2.0",
                "method": "notifications/message",
                "params": {
                    "level": "info",
                    "logger": "reference_python_server",
                    "data": f"Task {task_id} created with tool {tool}",
                },
            }
        )

        return {"taskId": task_id}

    @server.tool(name="tasks_list", description="List all background tasks")
    async def tasks_list() -> list:
        nonlocal tasks
        return list(tasks.values())

    @server.tool(name="tasks_get", description="Get a specific task by ID")
    async def tasks_get(taskId: str) -> dict:
        nonlocal tasks
        return tasks.get(taskId, {})

    @server.tool(name="tasks_cancel", description="Cancel a running task")
    async def tasks_cancel(taskId: str, ctx: Context) -> str:
        nonlocal tasks
        session = ctx.session

        if taskId in tasks:
            tasks[taskId]["status"] = "cancelled"

            # Emit cancelled notification
            await session.send_notification(
                {
                    "jsonrpc": "2.0",
                    "method": "notifications/message",
                    "params": {
                        "level": "info",
                        "logger": "reference_python_server",
                        "data": f"Task {taskId} cancelled",
                    },
                }
            )

            return "Task cancelled"

        return "Task not found"

    @server.resource(
        "resource://python-server/info",
        name="python-server-info",
        description="Reference server metadata",
    )
    def server_info() -> str:
        return "python reference server resource"

    @server.resource(
        "resource://python-server/template/{item_id}",
        name="python-server-template",
        description="Template resource with dynamic item_id parameter",
    )
    def resource_template(item_id: str) -> str:
        return f"Template resource for item {item_id}"

    async def notify_resource_updated(uri: str) -> None:
        """Emit notification to all subscribers of a resource."""
        if uri in resource_subscriptions:
            for session_id in resource_subscriptions[uri]:
                # In real implementation, would send to specific session
                pass

    @server.tool(name="resources_subscribe", description="Subscribe to a resource")
    async def resources_subscribe(uri: str, ctx: Context) -> str:
        """Subscribe to a resource for updates."""
        nonlocal resource_subscriptions
        session_id = str(ctx.request_id)
        if uri not in resource_subscriptions:
            resource_subscriptions[uri] = set()
        resource_subscriptions[uri].add(session_id)
        return f"Subscribed to {uri}"

    @server.tool(
        name="resources_unsubscribe", description="Unsubscribe from a resource"
    )
    async def resources_unsubscribe(uri: str, ctx: Context) -> str:
        """Unsubscribe from a resource."""
        nonlocal resource_subscriptions
        session_id = str(ctx.request_id)
        if uri in resource_subscriptions and session_id in resource_subscriptions[uri]:
            resource_subscriptions[uri].remove(session_id)
        return f"Unsubscribed from {uri}"

    @server.tool(
        name="emit_resource_updated",
        description="Trigger resource updated notification for testing",
    )
    async def emit_resource_updated(uri: str) -> str:
        """Trigger resource updated notification for testing."""
        await notify_resource_updated(uri)
        return f"Emitted update for {uri}"

    @server.tool(
        name="emit_resources_list_changed",
        description="Trigger resources list changed notification",
    )
    async def emit_resources_list_changed(ctx: Context) -> str:
        """Trigger resources list changed notification."""
        session = ctx.session
        await session.send_notification(
            {
                "jsonrpc": "2.0",
                "method": "notifications/resources/list_changed",
                "params": {},
            }
        )
        return "Emitted resources/list_changed"

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
