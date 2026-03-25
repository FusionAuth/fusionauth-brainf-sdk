# 🧠 FusionAuth Brainf*ck SDK

*The only authentication SDK written in Brainf*ck. Yes, really.*

This is a fully functional FusionAuth SDK compiled from C into pure Brainf*ck via the ELVM toolchain. It started as an April Fools joke. It still is. But it actually works.

```
+---------+   +-----------+   +---------+   +-----+   +--------+
| C Code  |→→→| ELVM IR   |→→→| elc     |→→→| BF  |→→→| 14.7MB |
| ~1200L  |   |           |   |compiler |   |code |   |binary  |
+---------+   +-----------+   +---------+   +-----+   +--------+
     ↓                                          ↓
  (also builds to native x86-64 for sanity checking)
```

## Why?

Because someone asked if FusionAuth could be compiled to Brainf*ck, and the only correct answer is: *challenge accepted*.

This project exists to:
- Push the limits of esoteric language compilation
- Prove that with enough toolchains, anything is possible
- Provide the world's least practical authentication solution
- Celebrate the beautiful chaos of April Fools day (every day)
- Serve as a case study in extreme constraint-based programming

## Features

✅ **LOGIN** – Get a session token from FusionAuth
✅ **REFRESH** – Refresh an existing session token
✅ **GETUSER** – Fetch user details
✅ **REGISTER** – Register a new user account
✅ **CREATEUSER** – Admin user creation
✅ **INTERACTIVE REPL** – Configure and test commands
✅ **Wire Protocol** – Binary protocol with control byte markers
✅ **HTTP/HTTPS** – Full integration with FusionAuth API

## What You're Getting Into

| Metric | Value |
|--------|-------|
| C source lines | ~1,200 |
| Brainf*ck output size | 14.7 MB (~15 million chars) |
| CPU usage during execution | 100% (it's thinking *very* hard) |
| Estimated time for a login command | 5-30 seconds (depending on CPU) |
| Recommended use case | Making your team question your life choices |

## Prerequisites

- **macOS** (tested on Intel and ARM)
- **Docker** or **OrbStack** (for FusionAuth container)
- **Python 3.6+** (for the runner script)
- **gcc** (for compiling ELVM and C code)
- **git** (to clone dependencies)

**Note:** This has been tested on macOS with Docker/OrbStack. Linux and Windows support is untested and probably cursed. YMMV.

## Quick Start

### 1. Clone the Repository

```bash
git clone <repo-url>
cd fusionauth-brainf-sdk
```

### 2. Start FusionAuth

```bash
cd fusionauth
docker compose up -d
```

Wait for FusionAuth to be ready (check logs: `docker compose logs -f`).

### 3. Build ELVM Toolchain

```bash
git clone https://github.com/shinh/elvm
cd elvm
make
cd ..
```

This builds the C-to-IR-to-Brainf*ck compilation pipeline.

### 4. Build Native Binary (for testing sanity)

```bash
cd src
make clean && make native
```

This compiles to native x86-64 code. Much faster. Much more reasonable.

### 5. Build Brainf*ck Binary

```bash
make clean && make eir && make bf
```

This generates:
- `fusionauth_eir` – Intermediate representation (readable-ish)
- `fusionauth.bf` – Pure Brainf*ck (14.7 MB of `+-<>,.[]`)

**Warning:** This is a 15 million character file. Yes, you read that right.

### 6. Run It (Native Mode – Recommended)

For actual testing and development:

```bash
cd bin
python3 -m venv venv
. ./venv/bin/activate
python3 runner.py \
  --native ./fusionauth_native \
  --url http://localhost:9011 \
  --docker-exec fusionauth-brainf-sdk-fusionauth-1 \
  -v
```

This:
- Uses the native binary (not Brainf*ck)
- Connects to FusionAuth at http://localhost:9011
- Uses `docker exec curl` to work around OrbStack auth header stripping
- Shows verbose output

### 7. Run It (Brainf*ck Mode – For the Brave)

```bash
python3 runner.py \
  --bf-interpreter ./elvm/out/bfopt \
  --bf-program fusionauth.bf \
  --url http://localhost:9011 \
  --docker-exec fusionauth-brainf-sdk-fusionauth-1 \
  -v
```

This uses the actual Brainf*ck binary. **Prepare for 100% CPU usage and a very, very long wait.**

### 8. Configure and Test

Inside the interactive REPL that launches, you'll see:

```
[fusionauth-sdk]>
```

Configure your API key:

```
config bf-sdk-test-api-key-do-not-use-in-production-0000
```

Try logging in:

```
login test@brainf.io changeme123 e9fdb985-9173-4e01-9d73-ac2d60d1dc8e
```

Available commands:
- `login <email> <password> <app-id>` – Get a session token
- `refresh <refresh-token>` – Refresh a session
- `getuser <user-id>` – Fetch user by ID
- `register <email> <password> <first-name> <last-name> <app-id>` – Register user
- `createuser <email> <password>` – Admin: create a user
- `config <api-key>` – Set the API key
- `help` – Show available commands
- `exit` – Quit the REPL

## How It Works

### The Compilation Pipeline

1. **C Source** (`fusionauth_bf.c`) – Standard C code with FusionAuth API logic
2. **8cc** (part of ELVM) – C compiler that outputs ELVM IR
3. **ELVM IR** – Intermediate representation (readable-ish assembly-like code)
4. **elc** – Compiler that converts IR to Brainf*ck
5. **fusionauth.bf** – 14.7 MB of pure esoteric code
6. **bfopt** (Brainf*ck interpreter) – Executes the BF with some optimizations

### The Wire Protocol

Communication between the Python runner and the BF program uses a binary protocol:

- **SOH (0x01)** – HTTP request/response boundary marker
- **STX (0x02)** – User command marker (REPL input)
- **ETX (0x03)** – Display message marker

The runner:
1. Reads commands from the REPL (stdin)
2. Sends them to the BF program via stdin pipes
3. The BF program calls into HTTP logic
4. Responses are piped back to the REPL via stdout

### ELVM Constraints (Why This Was Hard)

The ELVM C compiler has severe limitations:

- ❌ No `malloc()` (only static memory)
- ❌ No `printf()` or standard `<stdio.h>`
- ❌ No `string.h` functions
- ❌ No `stdlib.h` beyond `getchar`/`putchar`
- ❌ 24-bit word size (not 32 or 64)
- ❌ No division `/` or multiplication `*` operators

**Solutions:**
- Implemented integer multiply and divide from scratch
- Managed memory manually with fixed-size buffers
- Built a custom string manipulation library
- Used only `getchar()` and `putchar()` for I/O

### The Runner (`runner.py`)

The Python runner orchestrates the whole thing:

1. **Spawns the BF interpreter** with stdin/stdout pipes
2. **Implements the REPL** with command parsing
3. **Manages the wire protocol** (control bytes)
4. **Routes HTTP requests** via `docker exec curl` or direct HTTP
5. **Handles response parsing** from the BF program

## Project Structure

```
fusionauth-brainf-sdk/
├── README.md                  # You are here
├── fusionauth.bf              # Pure Brainf*ck (14.7 MB)
├── sdk-doc.md                 # API docs
├── bin/                       # Executables
│   ├── runner.py              # Python runner/REPL
│   └── setup_fa.sh            # Full test setup
│
├── src/                       # Source C code and Makefile
│   ├── Makefile               # Build targets
│   └── fusionauth_bf.c        # Main C source (~1200 lines)
│
└── fusionauth/                # Source C code and Makefile
    ├── kickstart/
    │   └── kickstart.json     # Initializations for fusionauth
    └── docker-compose.yml     # FusionAuth container config
```

## Known Issues & Limitations

### Performance

- **Native mode:** ~1-2 seconds per command (reasonable)
- **Brainf*ck mode:** 5-30 seconds per command (or longer, depending on CPU)
- The BF interpreter pegs CPU at 100% and memory usage stays reasonable (~50-100 MB)
- Brainf*ck has zero optimizations for common operations; every addition is a loop

### OrbStack on macOS

Docker's port forwarding on macOS (via OrbStack) strips the `Authorization` header. **Workaround:** Use the `--docker-exec` flag, which routes HTTP requests via `docker exec curl` executed directly inside the container.

```bash
python3 runner.py \
  --native ./fusionauth_native \
  --url http://localhost:9011 \
  --docker-exec fusionauth-brainf-sdk-fusionauth-1 \
  -v
```

### Platform Support

- ✅ Tested on **macOS** (Intel + ARM)
- ❓ Linux – probably works, untested
- ❌ Windows – definitely cursed, not tested

### Memory & Size

- The Brainf*ck binary is 14.7 MB (15 million characters)
- Runtime memory usage is modest (~50-100 MB)
- Text editors may struggle to open the BF file. Don't.

### ELVM Constraints

- No recursive functions (limited stack depth)
- No pointers beyond array indexing
- Integer arithmetic is slow (implemented in BF)
- String operations are manual and verbose

## Tips for Survival

1. **Use native mode for development.** Brainf*ck mode is for proving a point.
2. **Have patience.** The BF program is thinking. A lot.
3. **Monitor CPU.** At 100% usage, it's working. It's just *very* slow.
4. **Don't try to open the BF file in your editor.** Seriously. 15 million characters.
5. **Use `--docker-exec` on macOS.** The OrbStack header stripping is not a bug, it's a feature (of chaos).
6. **Celebrate the absurdity.** This should not exist. Embrace it.

## License

This project is released under the MIT License because even absurd things deserve legal clarity.

## Credits

- **[Dan Moore](https://www.mooreds.com/wordpress/)** – For always bringing past ideas back to top of mind
- **FusionAuth** – For creating an authentication platform worth compiling to Brainf*ck
- **ELVM** (Esoteric Language Virtual Machine) – For the toolchain that made this possible
- **Brainf*ck** – For being delightfully, impossibly constrained
- **April Fools Day** – For inspiring questionable decisions

---

*If you're reading this, you've either discovered something truly special or you've made some poor life choices. Either way, welcome to the Brainf*ck SDK. May your CPUs spin fast and your patience be eternal.*
