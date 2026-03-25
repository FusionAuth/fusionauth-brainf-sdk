#ifdef __GNUC__
#include <stdio.h>
#endif
/*
 * fusionauth_bf.c
 *
 * A minimal FusionAuth REST API SDK written in ELVM/8cc-compatible C.
 * Designed to be compiled to Brainfuck via the ELVM toolchain:
 *
 *   $ ./8cc -S -o fusionauth.eir fusionauth_bf.c
 *   $ ./elc -bf fusionauth.eir > fusionauth.bf
 *   $ python3 runner.py | bfopt fusionauth.bf
 *
 * ELVM/8cc constraints observed:
 *   - No malloc/free (all static buffers)
 *   - No printf/sprintf (manual putchar-based output)
 *   - No string.h (hand-rolled string ops)
 *   - No stdlib beyond getchar/putchar
 *   - All I/O via single-byte stdin (getchar) / stdout (putchar)
 *   - 24-bit word size (ELVM standard)
 *
 * Wire protocol (between BF program and external runner):
 *   BF stdout -> runner: HTTP request text, terminated by \x01 (SOH)
 *   runner -> BF stdin: HTTP response body, terminated by \x01 (SOH)
 *   Commands from user are read from stdin, prefixed by \x02 (STX)
 *
 * Supported FusionAuth API operations:
 *   1. LOGIN     - POST /api/login
 *   2. REFRESH   - POST /api/jwt/refresh
 *   3. REGISTER  - POST /api/user/registration (existing user)
 *   4. GETUSER   - GET  /api/user/{userId}
 *   5. CREATEUSER - POST /api/user/registration (full: user + registration)
 *
 * Copyright 2026 - The Most Cursed SDK In CIAM History
 */

/* ================================================================
 * SECTION 1: ELVM-compatible primitives
 * No #include — we declare what we need inline.
 * ELVM's libc provides putchar and getchar.
 * ================================================================ */

int putchar(int c);
int getchar(void);

#ifndef __GNUC__
/* ELVM has no hardware multiply — implement via repeated doubling */
int __builtin_mul(int a, int b) {
    int v;
    int d[24];
    int r[24];
    int i;
    int e;
    int x;
    if (a < b) {
        v = a;
        a = b;
        b = v;
    }
    if (b == 1) return a;
    if (b == 0) return 0;
    i = 0;
    e = 1;
    v = a;
    while (1) {
        int ne;
        d[i] = v;
        r[i] = e;
        v = v + v;
        ne = e + e;
        if (ne < e || ne > b) break;
        e = ne;
        i = i + 1;
    }
    x = 0;
    while (1) {
        if (b >= r[i]) {
            x = x + d[i];
            b = b - r[i];
        }
        if (i == 0) break;
        i = i - 1;
    }
    return x;
}
#endif

/* We avoid the NULL macro; use 0 for null pointer checks */

/* ================================================================
 * SECTION 2: Buffer sizes and static storage
 * Everything is statically allocated. No heap.
 * ================================================================ */

#define BUF_SMALL  128
#define BUF_MED    512
#define BUF_LARGE  2048
#define BUF_HUGE   4096

/* Sentinel bytes for the wire protocol */
#define WIRE_END   1   /* SOH - end of HTTP request/response on wire */
#define CMD_START  2   /* STX - start of user command on stdin */
#define NEWLINE    10
#define CR         13

/* Global state */
char g_api_key[BUF_SMALL];
int  g_api_key_len;
char g_base_url[BUF_SMALL];  /* not used in wire — runner knows the URL */
char g_tenant_id[BUF_SMALL];
int  g_tenant_id_len;

/* Reusable I/O buffers */
char g_req_body[BUF_HUGE];
char g_resp_body[BUF_HUGE];
char g_path[BUF_MED];
char g_method[16];
char g_val_buf[BUF_LARGE];  /* extracted JSON values */
char g_tmp[BUF_MED];

/* Last login result cache */
char g_last_token[BUF_LARGE];
char g_last_refresh_token[BUF_MED];
char g_last_user_id[BUF_SMALL];

/* ================================================================
 * SECTION 3: String utilities (hand-rolled, no string.h)
 * ================================================================ */

int str_len(char *s) {
    int n;
    n = 0;
    while (s[n] != 0) {
        n = n + 1;
    }
    return n;
}

void str_copy(char *dst, char *src) {
    int i;
    i = 0;
    while (src[i] != 0) {
        dst[i] = src[i];
        i = i + 1;
    }
    dst[i] = 0;
}

void str_copy_n(char *dst, char *src, int n) {
    int i;
    i = 0;
    while (i < n) {
        dst[i] = src[i];
        i = i + 1;
    }
    dst[i] = 0;
}

int str_eq(char *a, char *b) {
    int i;
    i = 0;
    while (a[i] != 0 && b[i] != 0) {
        if (a[i] != b[i]) return 0;
        i = i + 1;
    }
    return a[i] == b[i];
}

int str_starts_with(char *s, char *prefix) {
    int i;
    i = 0;
    while (prefix[i] != 0) {
        if (s[i] != prefix[i]) return 0;
        i = i + 1;
    }
    return 1;
}

/* Append src to dst in-place. Returns new length of dst. */
int str_append(char *dst, char *src) {
    int di;
    int si;
    di = str_len(dst);
    si = 0;
    while (src[si] != 0) {
        dst[di] = src[si];
        di = di + 1;
        si = si + 1;
    }
    dst[di] = 0;
    return di;
}

/* Append a single char */
int str_append_ch(char *dst, int ch) {
    int di;
    di = str_len(dst);
    dst[di] = ch;
    dst[di + 1] = 0;
    return di + 1;
}

/* ================================================================
 * SECTION 4: Output helpers (putchar-based)
 * ================================================================ */

void emit_str(char *s) {
    int i;
    i = 0;
    while (s[i] != 0) {
        putchar(s[i]);
        i = i + 1;
    }
}

void emit_line(char *s) {
    emit_str(s);
    putchar(CR);
    putchar(NEWLINE);
}

void emit_char(int c) {
    putchar(c);
}

/* Division by 10 via repeated subtraction (ELVM has no / operator) */
int div10(int n) {
    int q;
    q = 0;
    while (n >= 10) {
        n = n - 10;
        q = q + 1;
    }
    return q;
}

/* Modulo 10 via repeated subtraction */
int mod10(int n) {
    while (n >= 10) {
        n = n - 10;
    }
    return n;
}

/* Emit an integer as decimal ASCII */
void emit_int(int n) {
    char digits[12];
    int i;
    int neg;

    if (n == 0) {
        putchar('0');
        return;
    }

    neg = 0;
    if (n < 0) {
        neg = 1;
        n = 0 - n;
    }

    i = 0;
    while (n > 0) {
        digits[i] = '0' + mod10(n);
        n = div10(n);
        i = i + 1;
    }

    if (neg) putchar('-');

    /* digits are reversed, emit backwards */
    while (i > 0) {
        i = i - 1;
        putchar(digits[i]);
    }
}

/* ================================================================
 * SECTION 5: Input helpers (getchar-based)
 * ================================================================ */

/* Read from stdin until sentinel byte. Returns length. */
int read_until(char *buf, int max, int sentinel) {
    int i;
    int c;
    i = 0;
    while (i < max - 1) {
        c = getchar();
        if (c < 0) break;        /* EOF */
        if (c == sentinel) break;
        buf[i] = c;
        i = i + 1;
    }
    buf[i] = 0;
    return i;
}

/* Read a line from stdin (terminated by \n). Returns length. */
int read_line(char *buf, int max) {
    int i;
    int c;
    i = 0;
    while (i < max - 1) {
        c = getchar();
        if (c < 0) break;
        if (c == NEWLINE) break;
        if (c == CR) continue;   /* skip \r */
        buf[i] = c;
        i = i + 1;
    }
    buf[i] = 0;
    return i;
}

/* ================================================================
 * SECTION 6: Minimal JSON builder
 * Constructs JSON strings character-by-character into a buffer.
 * No dynamic allocation — writes directly to g_req_body.
 *
 * Usage pattern:
 *   json_begin();
 *   json_add_string("loginId", email);
 *   json_add_string("password", pass);
 *   json_end();
 *   // g_req_body now contains {"loginId":"...","password":"..."}
 * ================================================================ */

int g_json_pos;     /* current write position in g_req_body */
int g_json_count;   /* number of fields written (for comma logic) */

void json_raw(int ch) {
    g_req_body[g_json_pos] = ch;
    g_json_pos = g_json_pos + 1;
}

void json_raw_str(char *s) {
    int i;
    i = 0;
    while (s[i] != 0) {
        json_raw(s[i]);
        i = i + 1;
    }
}

/* Emit a JSON-safe string (escapes quotes and backslashes) */
void json_escaped_str(char *s) {
    int i;
    int c;
    i = 0;
    while (s[i] != 0) {
        c = s[i];
        if (c == '"' || c == '\\') {
            json_raw('\\');
        }
        json_raw(c);
        i = i + 1;
    }
}

void json_begin(void) {
    g_json_pos = 0;
    g_json_count = 0;
    json_raw('{');
}

void json_begin_at(int pos) {
    g_json_pos = pos;
    json_raw('{');
}

void json_end(void) {
    json_raw('}');
    g_req_body[g_json_pos] = 0;
}

void json_comma(void) {
    if (g_json_count > 0) {
        json_raw(',');
    }
    g_json_count = g_json_count + 1;
}

void json_add_string(char *key, char *val) {
    json_comma();
    json_raw('"');
    json_raw_str(key);
    json_raw('"');
    json_raw(':');
    json_raw('"');
    json_escaped_str(val);
    json_raw('"');
}

void json_add_bool(char *key, int val) {
    json_comma();
    json_raw('"');
    json_raw_str(key);
    json_raw('"');
    json_raw(':');
    if (val) {
        json_raw_str("true");
    } else {
        json_raw_str("false");
    }
}

/* Begin a nested object: "key":{ */
void json_begin_object(char *key) {
    json_comma();
    json_raw('"');
    json_raw_str(key);
    json_raw('"');
    json_raw(':');
    json_raw('{');
    g_json_count = 0;  /* reset comma counter for nested scope */
}

/* End a nested object: } */
void json_end_object(void) {
    json_raw('}');
    g_json_count = 1;  /* ensure next sibling gets a comma */
}

/* Add a string array: "key":["v1","v2"] */
void json_add_string_array_1(char *key, char *v1) {
    json_comma();
    json_raw('"');
    json_raw_str(key);
    json_raw('"');
    json_raw(':');
    json_raw('[');
    json_raw('"');
    json_escaped_str(v1);
    json_raw('"');
    json_raw(']');
}

/* ================================================================
 * SECTION 7: Minimal JSON parser
 * A state-machine parser that extracts string values by key name.
 * Works character-by-character on g_resp_body.
 *
 * Handles:
 *   - Nested objects (tracks depth)
 *   - Quoted strings with escape sequences
 *   - Skips numbers, bools, nulls, arrays
 *
 * Limitation: only extracts top-level or known-depth string values.
 * This is intentional — keeps the code small for BF compilation.
 * ================================================================ */

/*
 * Find a string value for a given key in JSON text.
 * Searches for "key":"value" pattern.
 * Writes the value (unescaped) to out_buf.
 * Returns 1 if found, 0 if not.
 *
 * depth_target: 0 = top-level keys only
 *               1 = one level deep (inside first object)
 *              -1 = any depth (first match wins)
 */
int json_find_string(char *json, char *key, char *out_buf, int max_out, int depth_target) {
    int i;
    int j;
    int c;
    int depth;
    int in_string;
    int key_len;
    int state;
    /* state: 0=scanning, 1=reading key, 2=found key awaiting colon,
              3=awaiting value, 4=reading value string */

    key_len = str_len(key);
    depth = 0;
    in_string = 0;
    state = 0;
    i = 0;
    j = 0;

    while (json[i] != 0) {
        c = json[i];

        if (state == 4) {
            /* Reading the value string */
            if (c == '\\') {
                /* Escape sequence — take next char literally */
                i = i + 1;
                if (json[i] == 0) break;
                if (j < max_out - 1) {
                    c = json[i];
                    if (c == 'n') { out_buf[j] = NEWLINE; }
                    else if (c == 't') { out_buf[j] = 9; }
                    else { out_buf[j] = c; }
                    j = j + 1;
                }
            } else if (c == '"') {
                /* End of value string */
                out_buf[j] = 0;
                return 1;
            } else {
                if (j < max_out - 1) {
                    out_buf[j] = c;
                    j = j + 1;
                }
            }
            i = i + 1;
            continue;
        }

        if (state == 3) {
            /* Awaiting the value — skip whitespace */
            if (c == ' ' || c == NEWLINE || c == CR || c == 9) {
                i = i + 1;
                continue;
            }
            if (c == '"') {
                /* It's a string value — start reading */
                state = 4;
                j = 0;
                i = i + 1;
                continue;
            }
            /* Value is not a string (number, bool, null, object, array) */
            /* Abandon this match */
            state = 0;
            /* Don't advance i — reprocess this char in state 0 */
            continue;
        }

        if (state == 2) {
            /* Found key, awaiting colon */
            if (c == ' ' || c == NEWLINE || c == CR || c == 9) {
                i = i + 1;
                continue;
            }
            if (c == ':') {
                state = 3;
                i = i + 1;
                continue;
            }
            /* Unexpected — reset */
            state = 0;
            continue;
        }

        /* state == 0 or state == 1: scanning */

        if (c == '{') {
            depth = depth + 1;
            i = i + 1;
            continue;
        }
        if (c == '}') {
            depth = depth - 1;
            i = i + 1;
            continue;
        }
        if (c == '[') {
            /* Skip arrays — find matching ] */
            int adepth;
            adepth = 1;
            i = i + 1;
            while (json[i] != 0 && adepth > 0) {
                if (json[i] == '[') adepth = adepth + 1;
                if (json[i] == ']') adepth = adepth - 1;
                if (json[i] == '"') {
                    /* Skip string inside array */
                    i = i + 1;
                    while (json[i] != 0 && json[i] != '"') {
                        if (json[i] == '\\') i = i + 1;
                        i = i + 1;
                    }
                }
                i = i + 1;
            }
            continue;
        }

        if (c == '"') {
            /* Start of a key string — read it and check if it matches */
            int ki;
            int match;
            i = i + 1; /* skip opening quote */

            /* Check depth constraint */
            if (depth_target >= 0 && depth != depth_target + 1) {
                /* Wrong depth — skip this string */
                while (json[i] != 0 && json[i] != '"') {
                    if (json[i] == '\\') i = i + 1;
                    i = i + 1;
                }
                if (json[i] == '"') i = i + 1;
                continue;
            }

            /* Compare key character by character */
            ki = 0;
            match = 1;
            while (json[i] != 0 && json[i] != '"') {
                if (json[i] == '\\') {
                    i = i + 1;
                    if (json[i] == 0) break;
                }
                if (ki < key_len) {
                    if (json[i] != key[ki]) match = 0;
                } else {
                    match = 0; /* key in JSON is longer */
                }
                ki = ki + 1;
                i = i + 1;
            }
            if (ki != key_len) match = 0;
            if (json[i] == '"') i = i + 1; /* skip closing quote */

            if (match) {
                state = 2; /* found our key — look for colon */
            }
            continue;
        }

        /* Skip whitespace, commas, colons (when not in key-match mode) */
        i = i + 1;
    }

    out_buf[0] = 0;
    return 0;
}

/* Convenience: find at any depth */
int json_find(char *json, char *key, char *out_buf, int max_out) {
    return json_find_string(json, key, out_buf, max_out, -1);
}

/* Find a top-level key */
int json_find_top(char *json, char *key, char *out_buf, int max_out) {
    return json_find_string(json, key, out_buf, max_out, 0);
}

/* ================================================================
 * SECTION 8: HTTP request builder + wire protocol
 *
 * The BF program emits HTTP-like text to stdout. An external
 * runner script reads this, makes the actual HTTP call, and
 * pipes the response body back via stdin.
 *
 * Wire format (BF -> runner):
 *   METHOD\n
 *   PATH\n
 *   HEADER: value\n    (repeated, 0 or more)
 *   \n                 (blank line = end of headers)
 *   BODY              (may be empty)
 *   \x01              (SOH = end of request)
 *
 * Wire format (runner -> BF):
 *   STATUS_CODE\n      (e.g., "200")
 *   RESPONSE_BODY      (raw JSON)
 *   \x01               (SOH = end of response)
 * ================================================================ */

int g_last_status;   /* last HTTP status code received */

void http_begin(char *method, char *path) {
    str_copy(g_method, method);
    str_copy(g_path, path);
}

void http_send(char *body) {
    int body_len;

    /* Emit method */
    emit_str(g_method);
    putchar(NEWLINE);

    /* Emit path */
    emit_str(g_path);
    putchar(NEWLINE);

    /* Emit headers */
    emit_str("Content-Type: application/json");
    putchar(NEWLINE);

    if (g_api_key_len > 0) {
        emit_str("Authorization: ");
        emit_str(g_api_key);
        putchar(NEWLINE);
    }

    if (g_tenant_id_len > 0) {
        emit_str("X-FusionAuth-TenantId: ");
        emit_str(g_tenant_id);
        putchar(NEWLINE);
    }

    /* Content-Length */
    body_len = 0;
    if (body != 0) {
        body_len = str_len(body);
    }
    emit_str("Content-Length: ");
    emit_int(body_len);
    putchar(NEWLINE);

    /* Blank line = end of headers */
    putchar(NEWLINE);

    /* Body */
    if (body != 0 && body_len > 0) {
        emit_str(body);
    }

    /* Wire sentinel */
    putchar(WIRE_END);
}

/* Read the response from the runner */
int http_recv(void) {
    int c;
    int status;
    int t;
    int i;

    /* Read status code (digits until newline) */
    status = 0;
    while (1) {
        c = getchar();
        if (c < 0) return -1;
        if (c == NEWLINE) break;
        if (c >= '0' && c <= '9') {
            /* multiply by 10 without * operator */
            t = status;
            status = t + t + t + t + t + t + t + t + t + t + (c - '0');
        }
    }
    g_last_status = status;

    /* Read response body until sentinel */
    i = read_until(g_resp_body, BUF_HUGE, WIRE_END);

    return status;
}

/* Combined: send request, get response. Returns status code. */
int http_do(char *method, char *path, char *body) {
    http_begin(method, path);
    http_send(body);
    return http_recv();
}

/* ================================================================
 * SECTION 9: FusionAuth API operations
 *
 * Each function:
 *   1. Builds the JSON request body in g_req_body
 *   2. Sends the HTTP request
 *   3. Parses the JSON response
 *   4. Caches relevant values (token, user_id, etc.)
 *   5. Prints a human-readable result to stdout... wait, no.
 *      stdout is the wire. We use a "display" function that
 *      emits a special display message to the runner.
 * ================================================================ */

/* Display a message to the user (via runner's stderr channel)
 * Wire format: \x03 (ETX) followed by text, then \x03 again */
void display(char *msg) {
    putchar(3); /* ETX = display message */
    emit_str(msg);
    putchar(3);
}

void display_kv(char *key, char *val) {
    putchar(3);
    emit_str(key);
    emit_str(": ");
    emit_str(val);
    putchar(3);
}

void display_status(void) {
    putchar(3);
    emit_str("HTTP Status: ");
    /* Inline int-to-str for the display channel */
    if (g_last_status >= 200 && g_last_status < 300) {
        emit_str("OK (");
    } else {
        emit_str("ERROR (");
    }
    emit_int(g_last_status);
    emit_str(")");
    putchar(3);
}

/* ---------------------------------------------------------------
 * LOGIN: POST /api/login
 * Required: loginId, password
 * Optional: applicationId
 * --------------------------------------------------------------- */
int fa_login(char *login_id, char *password, char *app_id) {
    int status;

    json_begin();
    json_add_string("loginId", login_id);
    json_add_string("password", password);
    if (app_id[0] != 0) {
        json_add_string("applicationId", app_id);
    }
    json_end();

    status = http_do("POST", "/api/login", g_req_body);
    display_status();

    if (status == 200) {
        /* Extract token */
        if (json_find_top(g_resp_body, "token", g_last_token, BUF_LARGE)) {
            display_kv("Access Token", g_last_token);
        }
        /* Extract refreshToken if present */
        if (json_find_top(g_resp_body, "refreshToken", g_last_refresh_token, BUF_MED)) {
            display_kv("Refresh Token", g_last_refresh_token);
        }
        /* Extract user.id */
        if (json_find(g_resp_body, "id", g_last_user_id, BUF_SMALL)) {
            display_kv("User ID", g_last_user_id);
        }
        display("Login successful.");
        return 1;
    }
    if (status == 202) {
        display("Login successful, but email not yet verified.");
        return 1;
    }
    if (status == 203) {
        display("Login successful, but password change required.");
        return 1;
    }
    if (status == 212) {
        display("Login successful, but email verification required.");
        return 1;
    }
    if (status == 242) {
        display("Two-factor challenge required.");
        /* Extract twoFactorId for the caller */
        json_find(g_resp_body, "twoFactorId", g_val_buf, BUF_LARGE);
        display_kv("Two Factor ID", g_val_buf);
        return 2;
    }
    if (status == 404) {
        display("User not found.");
        return 0;
    }

    display("Login failed.");
    return 0;
}

/* ---------------------------------------------------------------
 * REFRESH: POST /api/jwt/refresh
 * Required: refreshToken
 * --------------------------------------------------------------- */
int fa_refresh(char *refresh_token) {
    int status;

    json_begin();
    json_add_string("refreshToken", refresh_token);
    json_end();

    status = http_do("POST", "/api/jwt/refresh", g_req_body);
    display_status();

    if (status == 200) {
        if (json_find_top(g_resp_body, "token", g_last_token, BUF_LARGE)) {
            display_kv("New Access Token", g_last_token);
        }
        if (json_find_top(g_resp_body, "refreshToken", g_last_refresh_token, BUF_MED)) {
            display_kv("New Refresh Token", g_last_refresh_token);
        }
        display("Token refresh successful.");
        return 1;
    }

    display("Token refresh failed.");
    return 0;
}

/* ---------------------------------------------------------------
 * GET USER: GET /api/user/{userId}
 * Required: userId (UUID string)
 * --------------------------------------------------------------- */
int fa_get_user(char *user_id) {
    int status;
    char path[BUF_MED];

    /* Build path: /api/user/ + userId */
    str_copy(path, "/api/user/");
    str_append(path, user_id);

    /* GET has no body */
    g_req_body[0] = 0;

    status = http_do("GET", path, g_req_body);
    display_status();

    if (status == 200) {
        /* Extract interesting fields from user object */
        if (json_find(g_resp_body, "email", g_val_buf, BUF_LARGE)) {
            display_kv("Email", g_val_buf);
        }
        if (json_find(g_resp_body, "firstName", g_val_buf, BUF_LARGE)) {
            display_kv("First Name", g_val_buf);
        }
        if (json_find(g_resp_body, "lastName", g_val_buf, BUF_LARGE)) {
            display_kv("Last Name", g_val_buf);
        }
        if (json_find(g_resp_body, "username", g_val_buf, BUF_LARGE)) {
            display_kv("Username", g_val_buf);
        }
        if (json_find(g_resp_body, "id", g_val_buf, BUF_LARGE)) {
            display_kv("User ID", g_val_buf);
        }
        if (json_find(g_resp_body, "active", g_val_buf, BUF_LARGE)) {
            display_kv("Active", g_val_buf);
        }
        display("User retrieved successfully.");
        return 1;
    }
    if (status == 404) {
        display("User not found.");
        return 0;
    }

    display("Failed to retrieve user.");
    return 0;
}

/* ---------------------------------------------------------------
 * REGISTER (existing user): POST /api/user/registration
 * Required: userId, applicationId
 * Optional: roles (single role for simplicity)
 * --------------------------------------------------------------- */
int fa_register(char *user_id, char *app_id, char *role) {
    int status;
    char path[BUF_MED];

    /* Build path: /api/user/registration/ + userId */
    str_copy(path, "/api/user/registration/");
    str_append(path, user_id);

    json_begin();
    json_begin_object("registration");
    json_add_string("applicationId", app_id);
    if (role[0] != 0) {
        json_add_string_array_1("roles", role);
    }
    json_end_object();
    json_end();

    status = http_do("POST", path, g_req_body);
    display_status();

    if (status == 200) {
        display("Registration successful.");
        if (json_find_top(g_resp_body, "token", g_last_token, BUF_LARGE)) {
            display_kv("Token", g_last_token);
        }
        return 1;
    }

    display("Registration failed.");
    return 0;
}

/* ---------------------------------------------------------------
 * CREATE USER + REGISTER: POST /api/user/registration
 * Required: email, password, applicationId
 * Optional: firstName, lastName
 * --------------------------------------------------------------- */
int fa_create_user(char *email, char *password, char *app_id,
                   char *first_name, char *last_name) {
    int status;

    json_begin();

    /* User object */
    json_begin_object("user");
    json_add_string("email", email);
    json_add_string("password", password);
    if (first_name[0] != 0) {
        json_add_string("firstName", first_name);
    }
    if (last_name[0] != 0) {
        json_add_string("lastName", last_name);
    }
    json_end_object();

    /* Registration object */
    json_begin_object("registration");
    json_add_string("applicationId", app_id);
    json_end_object();

    json_add_bool("skipRegistrationVerification", 1);
    json_add_bool("skipVerification", 1);

    json_end();

    status = http_do("POST", "/api/user/registration", g_req_body);
    display_status();

    if (status == 200) {
        if (json_find_top(g_resp_body, "token", g_last_token, BUF_LARGE)) {
            display_kv("Token", g_last_token);
        }
        if (json_find(g_resp_body, "id", g_last_user_id, BUF_SMALL)) {
            display_kv("User ID", g_last_user_id);
        }
        if (json_find(g_resp_body, "email", g_val_buf, BUF_LARGE)) {
            display_kv("Email", g_val_buf);
        }
        display("User created and registered.");
        return 1;
    }

    display("User creation failed.");
    /* Try to show error details */
    if (json_find(g_resp_body, "message", g_val_buf, BUF_LARGE)) {
        display_kv("Error", g_val_buf);
    }
    return 0;
}

/* ================================================================
 * SECTION 10: Command REPL
 *
 * Reads commands from stdin via the runner.
 * Commands are prefixed with STX (\x02) to distinguish them
 * from HTTP response data.
 *
 * Command format (text lines, fields separated by \x1F (US)):
 *   \x02COMMAND\x1Farg1\x1Farg2\x1F...\n
 *
 * Supported commands:
 *   CONFIG\x1Fapi_key\x1Ftenant_id
 *   LOGIN\x1Flogin_id\x1Fpassword\x1Fapplication_id
 *   REFRESH\x1Frefresh_token
 *   REFRESH     (no arg = use last refresh token)
 *   GETUSER\x1Fuser_id
 *   GETUSER     (no arg = use last user_id from login)
 *   REGISTER\x1Fuser_id\x1Fapp_id\x1Frole
 *   CREATEUSER\x1Femail\x1Fpassword\x1Fapp_id\x1Ffirst\x1Flast
 *   QUIT
 * ================================================================ */

#define FIELD_SEP  31  /* Unit Separator - field delimiter */
#define MAX_ARGS   8

char g_cmd_buf[BUF_LARGE];
char g_args[MAX_ARGS][BUF_MED];
int  g_argc;

/* Parse command buffer into g_args array. Returns arg count. */
int parse_cmd(void) {
    int i;
    int ai;   /* current arg index */
    int ci;   /* current char index within arg */

    ai = 0;
    ci = 0;

    i = 0;
    while (g_cmd_buf[i] != 0 && ai < MAX_ARGS) {
        if (g_cmd_buf[i] == FIELD_SEP) {
            g_args[ai][ci] = 0;
            ai = ai + 1;
            ci = 0;
        } else {
            if (ci < BUF_MED - 1) {
                g_args[ai][ci] = g_cmd_buf[i];
                ci = ci + 1;
            }
        }
        i = i + 1;
    }
    /* Terminate last arg */
    g_args[ai][ci] = 0;
    if (ci > 0 || ai > 0) {
        ai = ai + 1;
    }
    g_argc = ai;

    /* Zero out unused args */
    while (ai < MAX_ARGS) {
        g_args[ai][0] = 0;
        ai = ai + 1;
    }

    return g_argc;
}

void cmd_loop(void) {
    int c;
    int running;

    running = 1;

    display("=== FusionAuth Brainfuck SDK v1.0 ===");
    display("Waiting for commands...");

    while (running) {
        /* Wait for command start marker */
        c = getchar();
        if (c < 0) break;  /* EOF = done */

        if (c != CMD_START) continue;  /* skip non-command bytes */

        /* Read command line */
        read_until(g_cmd_buf, BUF_LARGE, NEWLINE);
        parse_cmd();

        if (g_argc < 1) continue;

        /* Dispatch */
        if (str_eq(g_args[0], "CONFIG")) {
            if (g_argc >= 2) {
                str_copy(g_api_key, g_args[1]);
                g_api_key_len = str_len(g_api_key);
            }
            if (g_argc >= 3) {
                str_copy(g_tenant_id, g_args[2]);
                g_tenant_id_len = str_len(g_tenant_id);
            }
            display("Configuration updated.");
            display_kv("API Key", g_api_key);
            if (g_tenant_id_len > 0) {
                display_kv("Tenant ID", g_tenant_id);
            }
        }
        else if (str_eq(g_args[0], "LOGIN")) {
            if (g_argc >= 3) {
                fa_login(g_args[1], g_args[2],
                         g_argc >= 4 ? g_args[3] : g_args[0] + str_len(g_args[0]) /* empty str trick */);
                /* The empty str trick: pointer to the \0 terminator of args[0] */
            } else {
                display("Usage: LOGIN<US>loginId<US>password[<US>appId]");
            }
        }
        else if (str_eq(g_args[0], "REFRESH")) {
            if (g_argc >= 2 && g_args[1][0] != 0) {
                fa_refresh(g_args[1]);
            } else if (g_last_refresh_token[0] != 0) {
                fa_refresh(g_last_refresh_token);
            } else {
                display("No refresh token. Login first or provide one.");
            }
        }
        else if (str_eq(g_args[0], "GETUSER")) {
            if (g_argc >= 2 && g_args[1][0] != 0) {
                fa_get_user(g_args[1]);
            } else if (g_last_user_id[0] != 0) {
                fa_get_user(g_last_user_id);
            } else {
                display("No user ID. Login first or provide one.");
            }
        }
        else if (str_eq(g_args[0], "REGISTER")) {
            if (g_argc >= 3) {
                fa_register(g_args[1], g_args[2],
                            g_argc >= 4 ? g_args[3] : g_args[0] + str_len(g_args[0]));
            } else {
                display("Usage: REGISTER<US>userId<US>appId[<US>role]");
            }
        }
        else if (str_eq(g_args[0], "CREATEUSER")) {
            if (g_argc >= 4) {
                fa_create_user(
                    g_args[1],  /* email */
                    g_args[2],  /* password */
                    g_args[3],  /* appId */
                    g_argc >= 5 ? g_args[4] : g_args[0] + str_len(g_args[0]),
                    g_argc >= 6 ? g_args[5] : g_args[0] + str_len(g_args[0])
                );
            } else {
                display("Usage: CREATEUSER<US>email<US>pass<US>appId[<US>first<US>last]");
            }
        }
        else if (str_eq(g_args[0], "TOKEN")) {
            /* Print the last cached token */
            if (g_last_token[0] != 0) {
                display_kv("Cached Token", g_last_token);
            } else {
                display("No token cached. Login first.");
            }
        }
        else if (str_eq(g_args[0], "RAWRESP")) {
            /* Dump the last raw response for debugging */
            display_kv("Last Response", g_resp_body);
        }
        else if (str_eq(g_args[0], "QUIT")) {
            display("Goodbye from the Brainfuck SDK!");
            running = 0;
        }
        else {
            display("Unknown command. Available: CONFIG, LOGIN, REFRESH, GETUSER, REGISTER, CREATEUSER, TOKEN, RAWRESP, QUIT");
        }
    }
}

/* ================================================================
 * SECTION 11: Entry point
 * ================================================================ */

int main(void) {
    /* Initialize globals */
    g_api_key[0] = 0;
    g_api_key_len = 0;
    g_tenant_id[0] = 0;
    g_tenant_id_len = 0;
    g_last_token[0] = 0;
    g_last_refresh_token[0] = 0;
    g_last_user_id[0] = 0;
    g_last_status = 0;
    g_resp_body[0] = 0;
    g_req_body[0] = 0;

    /* Enter command loop */
    cmd_loop();

    return 0;
}