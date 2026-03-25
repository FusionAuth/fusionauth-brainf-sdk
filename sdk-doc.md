# FusionAuth Brainfuck Client Library

## Brainfuck Client Library

The Brainfuck client library allows you to integrate FusionAuth with your Brainfuck application.

We are sorry.

Source code:

- https://github.com/FusionAuth/fusionauth-brainfuck-sdk

#### Prerequisites

- macOS with Apple Silicon (tested on M4 Pro)
- [Docker](https://docs.docker.com/get-docker/) with [OrbStack](https://orbstack.dev/) or Docker Desktop
- Python 3.x
- gcc (included with Xcode Command Line Tools)
- git
- Patience

#### Installation

Clone the repository:

```
git clone https://github.com/FusionAuth/fusionauth-brainfuck-sdk.git
cd fusionauth-brainfuck-sdk
```

Build the ELVM toolchain (compiles C to Brainfuck):

```
git clone https://github.com/shinh/elvm
cd elvm && make && cd ..
```

The `make` command will produce some errors for missing language runtimes (scala, crystal, etc). These are expected and can be ignored. The tools you need — `8cc`, `elc`, `eli`, and `bfopt` — will compile successfully.

Compile the SDK to Brainfuck:

```
make clean && make eir && make bf
```

This produces `fusionauth.bf`, a 14.7 MB file containing approximately 15 million Brainfuck instructions. This is your SDK. Treat it with the reverence and horror it deserves.

#### Running FusionAuth

The repository includes a Docker Compose configuration with [Kickstart](https://fusionauth.io/docs/get-started/download-and-install/development/kickstart) that sets up FusionAuth with a test application, API key, and users.

```
docker compose up -d
```

FusionAuth will be initially configured with these settings:

- Your API key is `bf-sdk-test-api-key-do-not-use-in-production-0000`.
- Your application Id is `e9fdb985-9173-4e01-9d73-ac2d60d1dc8e`.
- Your example username is `test@brainfuck.io` and the password is `changeme123`.
- Your admin username is `admin@example.com` and the password is `password`.
- The base URL of FusionAuth is `http://localhost:9011/`.

> **caution** If you are running on macOS with OrbStack, HTTP Authorization headers may be stripped by port forwarding. The runner script includes a `--docker-exec` flag that routes HTTP calls through the container's network to work around this. See the [OrbStack Workaround](#orbstack-workaround) section below.

> **note** If you ever want to reset the FusionAuth application, you need to delete the volumes created by Docker Compose by executing `docker compose down -v`, then re-run `docker compose up -d`.

#### Example Usage

The following code assumes FusionAuth is running on `http://localhost:9011` and uses the API key `bf-sdk-test-api-key-do-not-use-in-production-0000`. You will need to supply your own API key if you are not using the provided Kickstart configuration.

**Native mode** (recommended for testing — runs the C binary directly, no Brainfuck interpreter):

```
make native
python3 runner.py --native ./fusionauth_native \
  --url http://localhost:9011 \
  --docker-exec fusionauth-brainfuck-sdk-fusionauth-1 -v
```

**Brainfuck mode** (the real deal — extremely slow):

```
python3 runner.py --bf-interpreter ./elvm/out/bfopt \
  --bf-program fusionauth.bf \
  --url http://localhost:9011 \
  --docker-exec fusionauth-brainfuck-sdk-fusionauth-1 -v
```

> **caution** Brainfuck mode will use 100% of one CPU core for an extended period. "Extended period" means minutes to hours depending on the operation. The native binary performs identically in terms of functionality and is recommended for all use cases where you value your time.

Once the interactive REPL appears, configure the SDK and authenticate a user:

```
bf-fusionauth> config bf-sdk-test-api-key-do-not-use-in-production-0000
bf-fusionauth> login test@brainfuck.io changeme123 e9fdb985-9173-4e01-9d73-ac2d60d1dc8e
```

Here is the full list of supported commands:

| Command | FusionAuth API | Description |
|---------|---------------|-------------|
| `config <api_key> [tenant_id]` | N/A | Configure the SDK with your API key |
| `login <email> <password> [app_id]` | `POST /api/login` | Authenticate a user |
| `refresh [refresh_token]` | `POST /api/jwt/refresh` | Refresh an access token |
| `getuser [user_id]` | `GET /api/user/{userId}` | Retrieve user details |
| `register <user_id> <app_id> [role]` | `POST /api/user/registration` | Register an existing user to an application |
| `createuser <email> <password> <app_id> [first] [last]` | `POST /api/user/registration` | Create a new user and register them |
| `token` | N/A | Display the cached access token |
| `rawresp` | N/A | Display the last raw API response |
| `quit` | N/A | Exit the SDK |

#### OrbStack Workaround

On Apple Silicon Macs running OrbStack, port forwarding can strip HTTP Authorization headers from requests. This causes all authenticated API calls to return `401 Unauthorized` even when credentials are correct.

The `--docker-exec` flag works around this by routing all HTTP requests through `docker exec curl` inside the FusionAuth container, bypassing OrbStack's port forwarding entirely.

```
# Without workaround (may fail with 401 on OrbStack)
python3 runner.py --native ./fusionauth_native --url http://localhost:9011

# With workaround (recommended on macOS/OrbStack)
python3 runner.py --native ./fusionauth_native \
  --url http://localhost:9011 \
  --docker-exec fusionauth-brainfuck-sdk-fusionauth-1
```

If you are not using OrbStack, or you are running on Linux, you may not need the `--docker-exec` flag.

#### Architecture

The Brainfuck SDK cannot make HTTP requests directly. Brainfuck supports only two I/O operations: read a byte from stdin (`,`) and write a byte to stdout (`.`).

The `runner.py` script acts as a bridge between the Brainfuck program and the FusionAuth REST API:

```
┌─────────────────────┐         ┌──────────────┐         ┌──────────────┐
│  Brainfuck Program  │  stdio  │  runner.py   │  HTTP   │  FusionAuth  │
│  (fusionauth.bf)    │◄───────►│  (Python)    │◄───────►│  Instance    │
└─────────────────────┘         └──────────────┘         └──────────────┘
```

The wire protocol uses ASCII control characters:

- **SOH (0x01)** — terminates HTTP request and response messages
- **STX (0x02)** — prefixes user commands from the REPL
- **ETX (0x03)** — wraps display messages shown to the user

The Brainfuck program constructs full HTTP requests (method, path, headers, body) and writes them to stdout. The runner parses these, makes the real HTTP call, and writes the response back to the Brainfuck program's stdin.

#### Compilation Pipeline

The SDK is written in C and compiled to Brainfuck through the [ELVM](https://github.com/shinh/elvm) (Esoteric Language Virtual Machine) toolchain:

```
fusionauth_bf.c → 8cc → fusionauth.eir → elc → fusionauth.bf
     1,200 lines    ELVM C       ELVM IR      ELVM         14.7 MB
                    compiler                  compiler     of Brainfuck
```

ELVM's C compiler (8cc) supports a restricted subset of C. The SDK source code is written within these constraints: no dynamic memory allocation, no standard library functions, no multiplication or division operators, and only `getchar()`/`putchar()` for I/O. All string handling, JSON parsing, JSON building, and HTTP request construction are implemented from scratch.

#### Usage Suggestions

The Brainfuck client library is a thin wrapper around the FusionAuth REST API, in the same way that a space shuttle is a thin wrapper around a controlled explosion.

The client library is typically used in one way: as a conversation starter. However, it is a fully functional SDK that supports core FusionAuth operations.

To use the client library effectively, it is helpful to review the [FusionAuth API documentation](https://fusionauth.io/docs/apis/), which contains the JSON structures the SDK constructs internally.

You can always directly call the REST API if the Brainfuck client library doesn't work for you. This is expected. The REST API does not require 100% CPU utilization to process an API key.

The response object will typically contain:

- a status corresponding to the HTTP status code returned by the API
- a JSON success object if the call succeeded
- a JSON error object with an intelligible message if the status code is `4xx` or `5xx`
- warmth radiating from your laptop as the Brainfuck interpreter processes the response

#### Known Limitations

- Brainfuck mode execution time is measured in minutes to hours per operation
- The SDK has been tested on macOS with Docker/OrbStack only
- Buffer sizes are statically allocated (4 KB for request/response bodies, 2 KB for tokens)
- The JSON parser extracts string values only; numeric and boolean values in responses are not parsed
- There is no TLS support in the Brainfuck program itself; TLS is handled by the runner
- You should not use this in production
- You should definitely not use this in production
