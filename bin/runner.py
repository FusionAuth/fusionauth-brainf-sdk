#!/usr/bin/env python3
"""
runner.py — Bridge between the FusionAuth Brainfuck SDK and real HTTP.

This script manages the bidirectional pipe between the BF program
(running in a BF interpreter) and the FusionAuth REST API.

Wire protocol:
  BF stdout -> runner:
    HTTP requests:  METHOD\n PATH\n HEADERS\n \n BODY \x01
    Display msgs:   \x03 message text \x03

  Runner -> BF stdin:
    HTTP responses: STATUS_CODE\n RESPONSE_BODY \x01
    User commands:  \x02 COMMAND\x1Farg1\x1Farg2... \n

Usage:
  python3 runner.py [--bf-interpreter bfopt] [--bf-program fusionauth.bf]
                    [--url https://your-instance.fusionauth.io]

  Or for testing without BF compilation (direct C execution via ELVM IR):
  python3 runner.py --eir-interpreter ./eli --eir-program fusionauth.eir
"""

import subprocess
import sys
import os
import threading
import queue
import json
import argparse
import readline  # for nicer input editing

try:
    import requests as requests_lib
except ImportError:
    requests_lib = None

# Wire protocol constants
WIRE_END = b'\x01'    # SOH - end of HTTP request/response
CMD_START = b'\x02'   # STX - start of user command
DISPLAY = b'\x03'     # ETX - display message
FIELD_SEP = '\x1f'    # Unit Separator

class BFRunner:
    def __init__(self, bf_cmd, fusionauth_url, verbose=False, docker_exec=None):
        self.fusionauth_url = fusionauth_url.rstrip('/')
        self.verbose = verbose
        self.docker_exec = docker_exec  # container name for docker exec curl
        self.display_queue = queue.Queue()
        self.proc = subprocess.Popen(
            bf_cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            bufsize=0  # unbuffered
        )
        # Start the output reader thread
        self.reader_thread = threading.Thread(
            target=self._read_bf_output, daemon=True
        )
        self.reader_thread.start()

    def _read_bf_output(self):
        """Read BF program's stdout and handle HTTP requests + display messages."""
        buf = b""
        while True:
            try:
                byte = self.proc.stdout.read(1)
                if not byte:
                    break

                if byte == DISPLAY:
                    # Read display message until next ETX
                    msg = b""
                    while True:
                        b = self.proc.stdout.read(1)
                        if not b or b == DISPLAY:
                            break
                        msg += b
                    self.display_queue.put(('display', msg.decode('utf-8', errors='replace')))
                    continue

                if byte == WIRE_END:
                    # We have a complete HTTP request
                    self._handle_http_request(buf.decode('utf-8', errors='replace'))
                    buf = b""
                    continue

                buf += byte

            except Exception as e:
                self.display_queue.put(('error', f"Reader error: {e}"))
                break

        self.display_queue.put(('done', ''))

    def _handle_http_request(self, raw):
        """Parse the wire format HTTP request and make the real call."""
        lines = raw.split('\n')
        if len(lines) < 2:
            self._send_response(400, '{"error":"malformed request"}')
            return

        method = lines[0].strip()
        path = lines[1].strip()

        # Parse headers
        headers = {}
        body_start = 2
        for i in range(2, len(lines)):
            line = lines[i].strip()
            if line == '':
                body_start = i + 1
                break
            if ':' in line:
                key, val = line.split(':', 1)
                headers[key.strip()] = val.strip()

        # Body is everything after the blank line
        body = '\n'.join(lines[body_start:]) if body_start < len(lines) else ''

        url = self.fusionauth_url + path

        if self.verbose:
            print(f"\n  [HTTP] {method} {url}")
            if body:
                print(f"  [BODY] {body[:200]}{'...' if len(body) > 200 else ''}")

        if self.docker_exec:
            self._http_via_docker(method, url, headers, body)
        else:
            self._http_via_requests(method, url, headers, body)

    def _http_via_docker(self, method, url, headers, body):
        """Make HTTP request via docker exec curl (bypasses OrbStack header issues)."""
        try:
            curl_cmd = [
                'docker', 'exec', self.docker_exec,
                'curl', '-s',
                '-w', '\n%{http_code}',
                '-X', method,
            ]
            for key, val in headers.items():
                curl_cmd.extend(['-H', f'{key}: {val}'])
            if body:
                curl_cmd.extend(['-d', body])
            curl_cmd.append(url)

            if self.verbose:
                print(f"  [DOCKER] docker exec {self.docker_exec} curl ...")

            result = subprocess.run(
                curl_cmd,
                capture_output=True,
                text=True,
                timeout=30
            )

            output = result.stdout.strip()
            # Last line is the HTTP status code (from -w '\n%{http_code}')
            lines = output.rsplit('\n', 1)
            if len(lines) == 2:
                resp_body = lines[0]
                status_code = int(lines[1])
            elif output.isdigit():
                resp_body = ''
                status_code = int(output)
            else:
                resp_body = output
                status_code = 0

            if self.verbose:
                print(f"  [RESP] {status_code} ({len(resp_body)} bytes)")

            self._send_response(status_code, resp_body)

        except subprocess.TimeoutExpired:
            self.display_queue.put(('error', 'Docker exec curl timed out'))
            self._send_response(0, '{"error":"timeout"}')
        except Exception as e:
            self.display_queue.put(('error', f"Docker exec error: {e}"))
            self._send_response(500, f'{{"error":"{str(e)}"}}')

    def _http_via_requests(self, method, url, headers, body):
        """Make HTTP request via Python requests library."""
        if requests_lib is None:
            self.display_queue.put(('error', 'requests library not installed; use --docker-exec'))
            self._send_response(500, '{"error":"requests library not available"}')
            return
        try:
            resp = requests_lib.request(
                method=method,
                url=url,
                headers=headers,
                data=body.encode('utf-8') if body else None,
                timeout=30
            )

            if self.verbose:
                print(f"  [RESP] {resp.status_code} ({len(resp.text)} bytes)")

            self._send_response(resp.status_code, resp.text)

        except requests_lib.exceptions.ConnectionError as e:
            self.display_queue.put(('error', f"Connection failed: {e}"))
            self._send_response(0, '{"error":"connection_failed"}')
        except Exception as e:
            self.display_queue.put(('error', f"HTTP error: {e}"))
            self._send_response(500, f'{{"error":"{str(e)}"}}')

    def _send_response(self, status_code, body):
        """Send response back to BF program via stdin."""
        response = f"{status_code}\n{body}".encode('utf-8') + WIRE_END
        try:
            self.proc.stdin.write(response)
            self.proc.stdin.flush()
        except BrokenPipeError:
            pass

    def send_command(self, cmd, *args):
        """Send a user command to the BF program."""
        parts = [cmd] + list(args)
        line = FIELD_SEP.join(parts)
        data = CMD_START + line.encode('utf-8') + b'\n'
        try:
            self.proc.stdin.write(data)
            self.proc.stdin.flush()
        except BrokenPipeError:
            print("BF program has exited.")
            sys.exit(1)

    def drain_display(self, timeout=2.0):
        """Print any pending display messages from the BF program."""
        import time
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                msg_type, msg = self.display_queue.get(timeout=0.1)
                if msg_type == 'display':
                    print(f"  🧠 {msg}")
                elif msg_type == 'error':
                    print(f"  ❌ {msg}")
                elif msg_type == 'done':
                    return False
            except queue.Empty:
                break
        return True

    def is_alive(self):
        return self.proc.poll() is None


def interactive_loop(runner):
    """Interactive REPL for talking to the BF FusionAuth SDK."""
    print("╔══════════════════════════════════════════════════════╗")
    print("║  FusionAuth Brainfuck SDK — Interactive Runner      ║")
    print("║  The world's most cursed identity management tool   ║")
    print("╚══════════════════════════════════════════════════════╝")
    print()

    # Wait for initial display messages (BF programs may be very slow)
    runner.drain_display(timeout=120.0)

    print("\nCommands:")
    print("  config <api_key> [tenant_id]")
    print("  login <email> <password> [app_id]")
    print("  refresh [refresh_token]")
    print("  getuser [user_id]")
    print("  register <user_id> <app_id> [role]")
    print("  createuser <email> <password> <app_id> [first] [last]")
    print("  token        — show cached access token")
    print("  rawresp      — show last raw API response")
    print("  quit")
    print()

    while runner.is_alive():
        try:
            line = input("bf-fusionauth> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nExiting...")
            runner.send_command("QUIT")
            runner.drain_display(timeout=1.0)
            break

        if not line:
            continue

        parts = line.split()
        cmd = parts[0].upper()
        args = parts[1:]

        if cmd == 'CONFIG':
            if len(args) < 1:
                print("  Usage: config <api_key> [tenant_id]")
                continue
            runner.send_command("CONFIG", *args)

        elif cmd == 'LOGIN':
            if len(args) < 2:
                print("  Usage: login <email> <password> [app_id]")
                continue
            runner.send_command("LOGIN", *args)

        elif cmd == 'REFRESH':
            runner.send_command("REFRESH", *args)

        elif cmd == 'GETUSER':
            runner.send_command("GETUSER", *args)

        elif cmd == 'REGISTER':
            if len(args) < 2:
                print("  Usage: register <user_id> <app_id> [role]")
                continue
            runner.send_command("REGISTER", *args)

        elif cmd == 'CREATEUSER':
            if len(args) < 3:
                print("  Usage: createuser <email> <password> <app_id> [first] [last]")
                continue
            runner.send_command("CREATEUSER", *args)

        elif cmd == 'TOKEN':
            runner.send_command("TOKEN")

        elif cmd == 'RAWRESP':
            runner.send_command("RAWRESP")

        elif cmd in ('QUIT', 'EXIT', 'Q'):
            runner.send_command("QUIT")
            runner.drain_display(timeout=1.0)
            break

        elif cmd == 'HELP':
            print("  Commands: config, login, refresh, getuser, register, createuser, token, rawresp, quit")
            continue

        else:
            print(f"  Unknown command: {cmd}. Type 'help' for commands.")
            continue

        # Wait for and display response (BF programs are very slow)
        runner.drain_display(timeout=120.0)


def main():
    parser = argparse.ArgumentParser(
        description='FusionAuth Brainfuck SDK Runner'
    )
    parser.add_argument(
        '--url', '-u',
        default=os.environ.get('FUSIONAUTH_URL', 'http://localhost:9011'),
        help='FusionAuth base URL (default: http://localhost:9011 or $FUSIONAUTH_URL)'
    )
    parser.add_argument(
        '--bf-interpreter', '-i',
        default='bfopt',
        help='Brainfuck interpreter command (default: bfopt)'
    )
    parser.add_argument(
        '--bf-program', '-p',
        default='fusionauth.bf',
        help='Compiled Brainfuck program (default: fusionauth.bf)'
    )
    parser.add_argument(
        '--eir-interpreter',
        help='Use ELVM IR interpreter instead of BF (for testing)'
    )
    parser.add_argument(
        '--eir-program',
        help='ELVM IR program file (for testing without BF compilation)'
    )
    parser.add_argument(
        '--native',
        help='Run natively compiled C binary (for testing without ELVM)'
    )
    parser.add_argument(
        '--docker-exec',
        metavar='CONTAINER',
        help='Route HTTP via "docker exec CONTAINER curl" (bypasses OrbStack header issues)'
    )
    parser.add_argument(
        '--verbose', '-v',
        action='store_true',
        help='Show HTTP request/response details'
    )

    args = parser.parse_args()

    # Determine what to run
    if args.native:
        bf_cmd = [args.native]
        print(f"Running native binary: {args.native}")
    elif args.eir_interpreter:
        prog = args.eir_program or 'fusionauth.eir'
        bf_cmd = [args.eir_interpreter, prog]
        print(f"Running via ELVM IR: {args.eir_interpreter} {prog}")
    else:
        bf_cmd = [args.bf_interpreter, args.bf_program]
        print(f"Running Brainfuck: {args.bf_interpreter} {args.bf_program}")

    print(f"FusionAuth URL: {args.url}")
    if args.docker_exec:
        print(f"HTTP via: docker exec {args.docker_exec} curl")
    else:
        print(f"HTTP via: Python requests")
    print()

    runner = BFRunner(bf_cmd, args.url, verbose=args.verbose, docker_exec=args.docker_exec)
    interactive_loop(runner)

    # Cleanup
    if runner.is_alive():
        runner.proc.terminate()


if __name__ == '__main__':
    main()