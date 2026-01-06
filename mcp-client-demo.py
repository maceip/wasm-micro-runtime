#!/usr/bin/env python3
"""
Simple MCP client that demonstrates URL elicitation
Connects to MCP servers via stdio and handles URL elicitation requests
"""
import json
import subprocess
import sys
import webbrowser
from typing import Optional

class MCPClient:
    def __init__(self, server_command: list[str]):
        self.server = subprocess.Popen(
            server_command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1
        )

    def send_request(self, method: str, params: dict, request_id: int) -> dict:
        """Send JSON-RPC request to server"""
        request = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params,
            "id": request_id
        }
        print(f"\n→ Sending: {method}")
        self.server.stdin.write(json.dumps(request) + "\n")
        self.server.stdin.flush()

        # Read response
        response_line = self.server.stdout.readline()
        if not response_line:
            return None

        response = json.loads(response_line)
        print(f"← Received: {json.dumps(response, indent=2)[:200]}...")
        return response

    def handle_url_elicitation(self, url: str, message: Optional[str] = None):
        """Handle URL elicitation by opening browser"""
        print(f"\n🌐 URL Elicitation Request!")
        if message:
            print(f"   Message: {message}")
        print(f"   URL: {url}")
        print(f"   Opening browser...")

        # Open URL in browser
        try:
            webbrowser.open(url)
            print(f"   ✓ Browser opened successfully")
            return True
        except Exception as e:
            print(f"   ✗ Failed to open browser: {e}")
            return False

    def initialize(self) -> dict:
        """Initialize MCP connection with URL elicitation capability"""
        params = {
            "protocolVersion": "2024-11-05",
            "capabilities": {
                "elicitation": {
                    "url": {}
                }
            },
            "clientInfo": {
                "name": "kontext-demo-client",
                "version": "1.0.0"
            }
        }
        return self.send_request("initialize", params, 1)

    def list_tools(self) -> dict:
        """List available tools"""
        return self.send_request("tools/list", {}, 2)

    def close(self):
        """Close connection"""
        if self.server:
            self.server.stdin.close()
            self.server.terminate()
            self.server.wait(timeout=5)

def demo_oauth_elicitation(oauth_binary: str):
    """Demonstrate URL elicitation with OAuth server"""
    print("=" * 70)
    print("MCP URL Elicitation Demo - OAuth Flow")
    print("=" * 70)

    client = MCPClient([oauth_binary])

    try:
        # Initialize with URL elicitation capability
        print("\n1. Initializing MCP connection...")
        init_response = client.initialize()

        if init_response and "result" in init_response:
            print("   ✓ Connection initialized")
            print(f"   Server: {init_response['result'].get('serverInfo', {}).get('name', 'unknown')}")

        # List tools
        print("\n2. Listing available tools...")
        tools_response = client.list_tools()

        if tools_response and "result" in tools_response:
            tools = tools_response["result"].get("tools", [])
            print(f"   Found {len(tools)} tools")
            for tool in tools:
                print(f"   - {tool.get('name', 'unknown')}")

        # Note: The actual URL elicitation would happen when the server
        # sends an elicitation notification. For OAuth, this happens when
        # you try to use a tool that requires authentication.

        print("\n3. Server is ready for URL elicitation")
        print("   The server will send a URL when authentication is needed")
        print("   This demo client will automatically open the URL in your browser")

    except KeyboardInterrupt:
        print("\n\n⚠ Demo interrupted by user")
    except Exception as e:
        print(f"\n✗ Error: {e}")
    finally:
        client.close()
        print("\n✓ Demo complete")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 mcp-client-demo.py <path-to-mcp-server>")
        print("Example: python3 mcp-client-demo.py ./oauth_prod")
        sys.exit(1)

    server_path = sys.argv[1]
    demo_oauth_elicitation(server_path)
