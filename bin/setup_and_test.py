#!/usr/bin/env python3
"""
setup_and_test.py — One-script setup for the FusionAuth Brainf*ck SDK.

Run this from inside the fusionauth-brainf-sdk folder with the venv activated:
    python3 setup_and_test.py

What it does:
  1. Stops any running FusionAuth containers and wipes data
  2. Starts fresh containers
  3. Waits for FusionAuth to be fully ready
  4. Tests the API key (with retries for cache warmup)
  5. Verifies the test user exists
  6. Optionally launches the SDK interactive session

Requires: docker, python3 with requests (in venv)
"""

import subprocess
import sys
import time
import os

try:
    import requests
except ImportError:
    print("ERROR: 'requests' not installed. Run:")
    print("  source venv/bin/activate")
    print("  pip install requests")
    sys.exit(1)

# Configuration
FUSIONAUTH_URL = "http://localhost:9011"
API_KEY = "bf-sdk-test-api-key-do-not-use-in-production-0000"
APP_ID = "e9fdb985-9173-4e01-9d73-ac2d60d1dc8e"
TEST_EMAIL = "test@brainf.io"
TEST_PASSWORD = "changeme123"
ADMIN_EMAIL = "admin@example.com"
ADMIN_PASSWORD = "password"


def run(cmd, check=True):
    """Run a shell command and return the result."""
    print(f"  $ {cmd}")
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if check and result.returncode != 0:
        if result.stderr:
            print(f"    stderr: {result.stderr.strip()}")
        return None
    return result


def wait_for_status(max_wait=120):
    """Wait for FusionAuth to report healthy status."""
    print(f"\n[2/6] Waiting for FusionAuth to start (up to {max_wait}s)...")
    start = time.time()
    while time.time() - start < max_wait:
        try:
            resp = requests.get(f"{FUSIONAUTH_URL}/api/status", timeout=3)
            if resp.status_code == 200:
                elapsed = int(time.time() - start)
                print(f"  FusionAuth is UP after {elapsed}s")
                return True
        except requests.exceptions.ConnectionError:
            pass
        except Exception as e:
            pass
        time.sleep(2)
        remaining = int(max_wait - (time.time() - start))
        if remaining % 10 == 0 and remaining > 0:
            print(f"  Still waiting... {remaining}s remaining")
    print("  TIMEOUT: FusionAuth did not start in time.")
    return False


def wait_for_kickstart(max_wait=60):
    """Wait for kickstart to complete by checking logs."""
    print(f"\n[3/6] Waiting for kickstart to complete (up to {max_wait}s)...")
    start = time.time()
    while time.time() - start < max_wait:
        result = subprocess.run(
            'docker compose logs fusionauth 2>&1 | grep -c "Summary:"',
            shell=True, capture_output=True, text=True
        )
        count = result.stdout.strip()
        if count and int(count) > 0:
            elapsed = int(time.time() - start)
            print(f"  Kickstart completed after {elapsed}s")
            # Show kickstart summary
            summary = subprocess.run(
                'docker compose logs fusionauth 2>&1 | grep -A 10 "Kickstarting"',
                shell=True, capture_output=True, text=True
            )
            for line in summary.stdout.strip().split('\n'):
                # Clean up docker compose log prefix
                clean = line.split('|', 1)[-1].strip() if '|' in line else line.strip()
                if clean:
                    print(f"    {clean}")
            return True
        time.sleep(2)
    print("  WARNING: Kickstart may not have run (no summary found in logs)")
    return False


def test_api_key(max_retries=10, delay=3):
    """Test the API key with retries (FusionAuth caches keys)."""
    print(f"\n[4/6] Testing API key (up to {max_retries} attempts, {delay}s apart)...")
    headers = {"Authorization": API_KEY}

    for attempt in range(1, max_retries + 1):
        try:
            resp = requests.get(
                f"{FUSIONAUTH_URL}/api/user?email={ADMIN_EMAIL}",
                headers=headers,
                timeout=10
            )
            if resp.status_code == 200:
                data = resp.json()
                if "user" in data:
                    email = data["user"].get("email", "unknown")
                    user_id = data["user"].get("id", "unknown")
                    print(f"  API key works! Found admin user: {email} (id: {user_id})")
                    return True
                else:
                    print(f"  Attempt {attempt}: Got 200 but no user in response")
            elif resp.status_code == 401:
                print(f"  Attempt {attempt}: 401 Unauthorized (API key not cached yet)")
            elif resp.status_code == 404:
                print(f"  Attempt {attempt}: 404 (admin user not found, but API key works!)")
                return True  # Key works, user just not found
            else:
                print(f"  Attempt {attempt}: HTTP {resp.status_code}")
                if resp.text:
                    print(f"    Response: {resp.text[:200]}")
        except requests.exceptions.ConnectionError:
            print(f"  Attempt {attempt}: Connection refused")
        except Exception as e:
            print(f"  Attempt {attempt}: {e}")

        if attempt < max_retries:
            time.sleep(delay)

    print("\n  FAILED: API key never worked after all retries.")
    print("  Let's check the logs for clues...")
    result = subprocess.run(
        'docker compose logs fusionauth 2>&1 | tail -30',
        shell=True, capture_output=True, text=True
    )
    print(result.stdout)
    return False


def test_user():
    """Verify the test user exists and can be found."""
    print(f"\n[5/6] Verifying test user ({TEST_EMAIL})...")
    headers = {"Authorization": API_KEY}
    try:
        resp = requests.get(
            f"{FUSIONAUTH_URL}/api/user?email={TEST_EMAIL}",
            headers=headers,
            timeout=10
        )
        if resp.status_code == 200:
            data = resp.json()
            user = data.get("user", {})
            print(f"  Found test user:")
            print(f"    Email: {user.get('email')}")
            print(f"    Name: {user.get('firstName')} {user.get('lastName')}")
            print(f"    User ID: {user.get('id')}")
            return user.get("id")
        else:
            print(f"  Test user not found (HTTP {resp.status_code})")
            return None
    except Exception as e:
        print(f"  Error: {e}")
        return None


def test_login():
    """Test that login works via the API (same thing the BF SDK will do)."""
    print(f"\n[6/6] Testing login API (what the BF SDK will call)...")
    try:
        resp = requests.post(
            f"{FUSIONAUTH_URL}/api/login",
            headers={
                "Authorization": API_KEY,
                "Content-Type": "application/json"
            },
            json={
                "loginId": TEST_EMAIL,
                "password": TEST_PASSWORD,
                "applicationId": APP_ID
            },
            timeout=10
        )
        if resp.status_code == 200:
            data = resp.json()
            token = data.get("token", "")
            refresh = data.get("refreshToken", "")
            print(f"  LOGIN SUCCESSFUL!")
            print(f"  JWT Token: {token[:50]}..." if len(token) > 50 else f"  JWT Token: {token}")
            if refresh:
                print(f"  Refresh Token: {refresh[:30]}...")
            else:
                print(f"  Refresh Token: (none - check JWT config on app)")
            return True
        elif resp.status_code == 202:
            print(f"  Login succeeded (202 - email not verified, that's OK)")
            return True
        elif resp.status_code == 212:
            print(f"  Login succeeded (212 - email verification required)")
            return True
        else:
            print(f"  Login failed: HTTP {resp.status_code}")
            print(f"  Response: {resp.text[:300]}")
            return False
    except Exception as e:
        print(f"  Error: {e}")
        return False


def main():
    print("=" * 60)
    print("  FusionAuth Brainf*ck SDK — Setup & Test")
    print("  The world's most cursed identity management tool")
    print("=" * 60)

    # Check we're in the right directory
    if not os.path.exists("docker-compose.yml"):
        print("\nERROR: Run this from the fusionauth-brainf-sdk folder.")
        print("  cd ~/fusionauth-brainf-sdk")
        print("  python3 setup_and_test.py")
        sys.exit(1)

    if not os.path.exists("kickstart/kickstart.json"):
        print("\nERROR: kickstart/kickstart.json not found.")
        sys.exit(1)

    # Step 1: Fresh start
    print("\n[1/6] Starting fresh (stopping containers, wiping data)...")
    run("docker compose down -v 2>&1", check=False)
    time.sleep(2)
    run("docker compose up -d 2>&1", check=False)

    # Step 2: Wait for FusionAuth
    if not wait_for_status(max_wait=120):
        print("\nFusionAuth failed to start. Check Docker/OrbStack is running.")
        sys.exit(1)

    # Step 3: Wait for kickstart
    wait_for_kickstart(max_wait=60)

    # Extra breathing room for API key cache
    print("\n  Waiting 10s for API key cache to warm up...")
    time.sleep(10)

    # Step 4: Test API key
    if not test_api_key(max_retries=10, delay=5):
        print("\n" + "=" * 60)
        print("  SETUP FAILED")
        print("  The API key is not working after multiple retries.")
        print("  Possible causes:")
        print("    - Kickstart didn't run properly")
        print("    - FusionAuth setup wizard is blocking API access")
        print("")
        print("  Try logging into the admin UI:")
        print(f"    URL: {FUSIONAUTH_URL}/admin")
        print(f"    Email: {ADMIN_EMAIL}")
        print(f"    Password: {ADMIN_PASSWORD}")
        print("=" * 60)
        sys.exit(1)

    # Step 5: Test user
    user_id = test_user()

    # Step 6: Test login
    login_ok = test_login()

    # Summary
    print("\n" + "=" * 60)
    if login_ok:
        print("  ALL TESTS PASSED!")
        print("")
        print("  Your FusionAuth instance is ready for the Brainf*ck SDK.")
        print("")
        print("  To launch the SDK:")
        print(f"    python3 runner.py --native ./fusionauth_native --url {FUSIONAUTH_URL} -v")
        print("")
        print("  Then in the SDK prompt:")
        print(f"    config {API_KEY}")
        print(f"    login {TEST_EMAIL} {TEST_PASSWORD} {APP_ID}")
        print("    getuser")
        print("    refresh")
        print("    quit")
        print("")
        print("  Configuration summary:")
        print(f"    FusionAuth URL: {FUSIONAUTH_URL}")
        print(f"    API Key: {API_KEY}")
        print(f"    App ID: {APP_ID}")
        print(f"    Test User: {TEST_EMAIL} / {TEST_PASSWORD}")
        if user_id:
            print(f"    User ID: {user_id}")
    else:
        print("  PARTIAL SUCCESS")
        print("  API key works but login test failed.")
        print("  The app may need JWT configuration.")
        print(f"  Log into {FUSIONAUTH_URL}/admin with {ADMIN_EMAIL}/{ADMIN_PASSWORD}")
        print("  and check the BF SDK Test App's JWT settings.")
    print("=" * 60)

    # Ask if they want to launch the SDK
    if login_ok and os.path.exists("fusionauth_native"):
        try:
            answer = input("\nLaunch the SDK now? (y/n): ").strip().lower()
            if answer in ('y', 'yes'):
                print("\nLaunching FusionAuth Brainf*ck SDK...\n")
                os.execvp("python3", [
                    "python3", "runner.py",
                    "--native", "./fusionauth_native",
                    "--url", FUSIONAUTH_URL,
                    "-v"
                ])
        except (EOFError, KeyboardInterrupt):
            print("\nDone.")


if __name__ == "__main__":
    main()
