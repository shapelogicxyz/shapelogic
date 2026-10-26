#!/usr/bin/env bash
# Staples Apple's notarisation ticket onto one architecture's macOS build,
# once Apple has accepted it: the Release workflow runs it after waiting for
# Apple, the Staple workflow (the fallback) when Apple took longer.
#
#   .github/scripts/staple.sh <notarisation record .json> <folder with the .dmg and .zip>
#
# The record is the one Release writes when it submits (notarisation-<arch>.json:
# submission_id, file, sha256). It checks the folder's .dmg is the file Apple
# was sent (its sha256 against the record and against Apple's log of the
# submission), staples the .dmg and the app inside each .zip (zipped again
# as electron-builder does), and checks Gatekeeper accepts the app as
# notarised. A submission still In Progress, or Invalid, fails with its
# status (and for Invalid, Apple's log); nothing is stapled then.
#
# Needs APPLE_API_KEY_P8, APPLE_API_KEY_ID and APPLE_API_ISSUER; the key file
# it writes in RUNNER_TEMP is removed when it ends.
set -euo pipefail

record="$1"
dir="$2"
tmp="${RUNNER_TEMP:-$(mktemp -d)}"

cat "${record}"
id="$(jq -r .submission_id "${record}")"
dmg="${dir}/$(jq -r .file "${record}")"
sha256="$(jq -r .sha256 "${record}")"
arch="$(jq -r .arch "${record}")"
if [ "$(shasum -a 256 "${dmg}" | cut -d' ' -f1)" != "${sha256}" ]; then
  echo "::error::${dmg} is not the file the run recorded sending to Apple"
  exit 1
fi

key_file="${tmp}/AuthKey_${APPLE_API_KEY_ID}.p8"
trap 'rm -f "${key_file}"' EXIT
printf '%s\n' "${APPLE_API_KEY_P8}" > "${key_file}"
chmod 600 "${key_file}"
auth=(--key "${key_file}" --key-id "${APPLE_API_KEY_ID}" --issuer "${APPLE_API_ISSUER}")

info="$(xcrun notarytool info "${id}" "${auth[@]}" --output-format json)"
echo "${info}"
status="$(jq -r .status <<< "${info}")"
case "${status}" in
  Accepted) ;;
  "In Progress")
    echo "::error::submission ${id} (${arch}, sent $(jq -r .createdDate <<< "${info}")) is still In Progress at Apple; run Staple once it is Accepted"
    exit 1 ;;
  *)
    xcrun notarytool log "${id}" "${auth[@]}" || true
    echo "::error::submission ${id} (${arch}) is ${status}: Apple's log is above"
    exit 1 ;;
esac
# Apple's own record of what it was sent: the same bytes as this .dmg
apple_sha256="$(xcrun notarytool log "${id}" "${auth[@]}" | jq -r .sha256)"
if [ "${apple_sha256}" != "${sha256}" ]; then
  echo "::error::Apple's log says submission ${id} was ${apple_sha256}, not this .dmg (${sha256})"
  exit 1
fi

xcrun stapler staple "${dmg}"
xcrun stapler validate "${dmg}"
mnt="$(mktemp -d)"
hdiutil attach -nobrowse -readonly -mountpoint "${mnt}" "${dmg}"
spctl -a -vv -t exec "${mnt}/ShapeLogic.app" 2>&1 | tee "${tmp}/spctl.txt"
hdiutil detach "${mnt}"
if ! grep -q "Notarized Developer ID" "${tmp}/spctl.txt"; then
  echo "::error::Gatekeeper does not take the app in ${dmg} as notarised"
  exit 1
fi

# the .zip holds the same app; staple it and zip it again as
# electron-builder does (ditto, the app at the top)
shopt -s nullglob
for zip in "${dir}"/*.zip; do
  unpacked="$(mktemp -d)"
  ditto -x -k "${zip}" "${unpacked}"
  xcrun stapler staple "${unpacked}/ShapeLogic.app"
  xcrun stapler validate "${unpacked}/ShapeLogic.app"
  rm "${zip}"
  ditto -c -k --sequesterRsrc --keepParent "${unpacked}/ShapeLogic.app" "${zip}"
done
(cd "${dir}" && shasum -a 256 ./*.dmg ./*.zip) | tee -a "${GITHUB_STEP_SUMMARY:-/dev/null}"
