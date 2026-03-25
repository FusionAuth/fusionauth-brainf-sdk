#!/bin/bash
# Run the FusionAuth Brainfuck SDK test via Docker network
# This bypasses OrbStack's port forwarding which strips Authorization headers
#
# Usage: bash run_test.sh

set -e

CONTAINER="fusionauth-brainfuck-sdk-fusionauth-1"
API_KEY="bf-sdk-test-api-key-do-not-use-in-production-0000"
APP_ID="e9fdb985-9173-4e01-9d73-ac2d60d1dc8e"
FA_URL="http://localhost:9011"

echo "=== FusionAuth Brainfuck SDK — Test Runner ==="
echo ""

# Verify FusionAuth is up and API key works (from inside container)
echo "Checking FusionAuth API key..."
STATUS=$(docker exec "$CONTAINER" curl -s -o /dev/null -w "%{http_code}" \
  -H "Authorization: $API_KEY" "$FA_URL/api/tenant")
if [ "$STATUS" != "200" ]; then
  echo "ERROR: API key check failed (HTTP $STATUS)"
  exit 1
fi
echo "API key works!"

echo ""
echo "Checking test user login..."
LOGIN_RESULT=$(docker exec "$CONTAINER" curl -s -w "\n%{http_code}" \
  -H "Content-Type: application/json" \
  "$FA_URL/api/login" \
  -d "{\"loginId\":\"test@brainfuck.io\",\"password\":\"changeme123\",\"applicationId\":\"$APP_ID\"}")
HTTP_CODE=$(echo "$LOGIN_RESULT" | tail -1)
echo "Login returned HTTP $HTTP_CODE"

if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "202" ]; then
  echo ""
  echo "=== FusionAuth is ready! ==="
  echo ""
  echo "API Key:        $API_KEY"
  echo "Application ID: $APP_ID"
  echo "FusionAuth URL: $FA_URL (from inside Docker network)"
  echo "Test user:      test@brainfuck.io / changeme123"
  echo ""
  echo "To run the native SDK test:"
  echo "  python3 runner.py --native ./fusionauth_native --url $FA_URL --docker-exec $CONTAINER -v"
else
  echo "Login test failed (HTTP $HTTP_CODE). Output:"
  echo "$LOGIN_RESULT" | head -5
fi
