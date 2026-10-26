# Maintainer: Tiago Tagliaferri <tiago_tagliaferri@msn.com>
pkgname=ms912x-ng-dkms
_source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if git -C "$_source_dir" rev-parse --verify HEAD >/dev/null 2>&1; then
	_git_revision="$(git -C "$_source_dir" rev-list --count HEAD)"
	_git_commit="$(git -C "$_source_dir" rev-parse --short=7 HEAD)"
	pkgver="r${_git_revision}.${_git_commit}"
	if [[ -n "$(git -C "$_source_dir" status --porcelain)" ]]; then
		pkgver="${pkgver}.dirty"
	fi
else
	IFS= read -r pkgver < "$_source_dir/VERSION"
fi
unset _git_revision _git_commit
pkgrel=1
pkgdesc="ms912x-ng DKMS DRM driver for MacroSilicon USB display adapters"
arch=('any')
url="https://github.com/taglia-ti/ms912x-ng"
license=('GPL-2.0-only')
depends=('dkms')
makedepends=('git')
optdepends=('linux-headers: build the module for the stock Arch kernel')
source=(
	LICENSE
	README.md
	AUTHORS
)
sha256sums=('SKIP' 'SKIP' 'SKIP')

build() {
	# Build from this checkout using the same source manifest as direct DKMS users.
	make -C "$_source_dir" dkms-package \
		DKMS_PACKAGE_NAME="${pkgname%-dkms}" \
		DKMS_PACKAGE_VERSION="$pkgver" DKMS_OUTPUT="$srcdir"
}

package() {
	local dkms_name="${pkgname%-dkms}"
	local dkms_dir="$pkgdir/usr/src/$dkms_name-$pkgver"

	install -dm755 "$pkgdir/usr/src"
	bsdtar --no-same-owner -xf "$srcdir/$dkms_name-$pkgver.tar.gz" -C "$pkgdir/usr/src"
	install -Dm644 "$srcdir/LICENSE" \
		"$pkgdir/usr/share/licenses/$pkgname/LICENSE"
	install -Dm644 "$srcdir/README.md" \
		"$pkgdir/usr/share/doc/$pkgname/README.md"
	install -Dm644 "$srcdir/AUTHORS" \
		"$pkgdir/usr/share/doc/$pkgname/AUTHORS"
	install -Dm644 "$dkms_dir/linux/udev/60-ms912x.rules" \
		"$pkgdir/usr/lib/udev/rules.d/60-ms912x.rules"
}
