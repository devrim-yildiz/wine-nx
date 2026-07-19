#!/bin/sh
# Fetch the third-party sample binaries that used to be committed to git
# (~16MB of prebuilt executables with no provenance). Every download is
# pinned to the sha256 of the exact bytes that were previously tracked, so
# a successful run reproduces the old checkout byte-for-byte.
#
# If a URL 404s (curl.se prunes old curl-for-win releases), that is a
# deliberate re-pin moment, not something to paper over: pick the new
# release, update BOTH the URL and the sha256 pins here, and re-test the
# curl smoke target on hardware before committing the bump.
set -eu

SAMPLES_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../samples" && pwd)"

CURL_VER="8.20.0_2"
CURL_ZIP="curl-${CURL_VER}-win64a-mingw.zip"
CURL_URL="https://curl.se/windows/dl-${CURL_VER}/${CURL_ZIP}"
CURL_ZIP_SHA256="5ce9abc57fe29a86e07e2d33b69b485e4b17fde80c4a8d8c213080e899a5d294"
CURL_EXE_SHA256="d2c7fb8b669f2526a3f349d9eb958999d57f161959c92ed967be4118fe2cce8e"
LIBCURL_DLL_SHA256="cfe545de69a8841eb1eba7db64795a873a0dd8e2023cdf55e8ad516335d4abed"
TRURL_EXE_SHA256="d68b39fb339b6cfb152d013cc24515674f2fc70828ee6a23abaf8833d79086bb"

SEVENZIP_EXE="7z2601-arm64.exe"
SEVENZIP_URL="https://www.7-zip.org/a/${SEVENZIP_EXE}"
SEVENZIP_SHA256="1fecf4e3407950939c8ffcc3e42e3039821997dea155301c75369474e5f15175"

sha256_of()
{
    if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1
    else sha256sum "$1" | cut -d' ' -f1
    fi
}

verify()
{
    actual="$(sha256_of "$1")"
    if [ "$actual" != "$2" ]; then
        echo "FATAL: sha256 mismatch for $1" >&2
        echo "  expected: $2" >&2
        echo "  actual:   $actual" >&2
        echo "Refusing to continue with unverified binaries." >&2
        exit 1
    fi
    echo "  ok: $1"
}

fetch()
{
    url="$1"; dest="$2"; sha="$3"
    if [ -f "$dest" ] && [ "$(sha256_of "$dest")" = "$sha" ]; then
        echo "  cached: $dest"
        return 0
    fi
    echo "  fetching $url"
    curl -fL --retry 3 -o "$dest.tmp" "$url"
    mv "$dest.tmp" "$dest"
    verify "$dest" "$sha"
}

echo "[1/3] 7-Zip ARM64 installer (used by WINE_NX_APP=7z-style smoke runs)"
fetch "$SEVENZIP_URL" "$SAMPLES_DIR/$SEVENZIP_EXE" "$SEVENZIP_SHA256"

echo "[2/3] curl-for-win ${CURL_VER} win64a (ARM64) release zip"
fetch "$CURL_URL" "$SAMPLES_DIR/$CURL_ZIP" "$CURL_ZIP_SHA256"

echo "[3/3] extracting curl.exe / libcurl-arm64.dll / trurl.exe (WINE_NX_APP=curl)"
mkdir -p "$SAMPLES_DIR/curl-arm64"
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
unzip -o -q "$SAMPLES_DIR/$CURL_ZIP" \
    "curl-${CURL_VER}-win64a-mingw/bin/curl.exe" \
    "curl-${CURL_VER}-win64a-mingw/bin/libcurl-arm64.dll" \
    "curl-${CURL_VER}-win64a-mingw/bin/trurl.exe" \
    -d "$tmpdir"
for f in curl.exe libcurl-arm64.dll trurl.exe; do
    cp "$tmpdir/curl-${CURL_VER}-win64a-mingw/bin/$f" "$SAMPLES_DIR/curl-arm64/$f"
done
verify "$SAMPLES_DIR/curl-arm64/curl.exe" "$CURL_EXE_SHA256"
verify "$SAMPLES_DIR/curl-arm64/libcurl-arm64.dll" "$LIBCURL_DLL_SHA256"
verify "$SAMPLES_DIR/curl-arm64/trurl.exe" "$TRURL_EXE_SHA256"

echo "done: all sample binaries fetched and verified."
