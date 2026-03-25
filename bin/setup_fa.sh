#!/bin/bash
# Setup script for FusionAuth Brainfuck SDK
# Run this AFTER completing the setup wizard in the browser
#
# Usage: bash setup_fa.sh <your-api-key>
#
# The API key is shown after you complete the setup wizard.

set -e

API_KEY="$1"
BASE="http://localhost:9011"
APP_ID="e9fdb985-9173-4e01-9d73-ac2d60d1dc8e"

if [ -z "$API_KEY" ]; then
  echo "Usage: bash setup_fa.sh <your-api-key>"
  echo ""
  echo "Complete the setup wizard at http://localhost:9011/admin/setup-wizard first."
  echo "Copy the API key it gives you, then run this script with that key."
  exit 1
fi

echo "=== Testing API key ==="
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -H "Authorization: $API_KEY" "$BASE/api/tenant")
if [ "$STATUS" != "200" ]; then
  echo "ERROR: API key returned HTTP $STATUS. Is the key correct?"
  exit 1
fi
echo "API key works! (HTTP 200)"

echo ""
echo "=== Creating test application ==="
curl -s -o /dev/null -w "HTTP %{http_code}\n" \
  -X POST \
  -H "Authorization: $API_KEY" \
  -H "Content-Type: application/json" \
  "$BASE/api/application/$APP_ID" \
  -d '{
    "application": {
      "name": "BF SDK Test App",
      "loginConfiguration": {
        "allowTokenRefresh": true,
        "generateRefreshTokens": true,
        "requireAuthentication": true
      },
      "registrationConfiguration": {
        "enabled": true,
        "type": "basic"
      }
    }
  }'

echo ""
echo "=== Creating test user ==="
curl -s -o /dev/null -w "HTTP %{http_code}\n" \
  -X POST \
  -H "Authorization: $API_KEY" \
  -H "Content-Type: application/json" \
  "$BASE/api/user/registration" \
  -d '{
    "skipRegistrationVerification": true,
    "user": {
      "email": "test@brainfuck.io",
      "firstName": "Brainfuck",
      "lastName": "User",
      "password": "changeme123"
    },
    "registration": {
      "applicationId": "'"$APP_ID"'"
    }
  }'

echo ""
echo "=== Testing login ==="
RESULT=$(curl -s -w "\n%{http_code}" \
  -H "Content-Type: application/json" \
  "$BASE/api/login" \
  -d '{
    "loginId": "test@brainfuck.io",
    "password": "changeme123",
    "applicationId": "'"$APP_ID"'"
  }')

HTTP_CODE=$(echo "$RESULT" | tail -1)
echo "Login returned HTTP $HTTP_CODE"

if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "202" ]; then
  echo ""
  echo "=== SUCCESS! ==="
  echo "FusionAuth is ready for the Brainfuck SDK."
  echo ""
  echo "API Key:        $API_KEY"
  echo "Application ID: $APP_ID"
  echo "Test user:      test@brainfuck.io / changeme123"
  echo ""
  echo "Next: python3 runner.py --native ./fusionauth_native --url $BASE -v"
else
  echo "Login failed. Check the output above for errors."
fi
