# Maintainer: Tiago Tagliaferri <tiago_tagliaferri@msn.com>
pkgname=ms912x-ng-dkms
_upstream_version=0.1.1
# Arch pkgver cannot contain the upstream prerelease separator.
pkgver=${_upstream_version//-/}
pkgrel=1
pkgdesc="ms912x-ng DKMS DRM driver for MacroSilicon USB display adapters"
arch=('any')
url="https://github.com/taglia-ti/ms912x-ng"
license=('GPL-2.0-only')
depends=('dkms' 'linux-headers')
source=("ms912x-ng-${_upstream_version}-source.tar.gz::${url}/archive/refs/tags/v${_upstream_version}.tar.gz")
sha256sums=('80a706ca67eb796f495b8388da6127dac32bb0a7e2c623dbd22581104fb3eded')

build() {
	make -C "$srcdir/ms912x-ng-$_upstream_version" dkms-package \
		DKMS_PACKAGE_NAME="${pkgname%-dkms}" \
		DKMS_PACKAGE_VERSION="$pkgver" DKMS_OUTPUT="$srcdir"
}

package() {
	local dkms_name="${pkgname%-dkms}"
	local dkms_dir="$pkgdir/usr/src/$dkms_name-$pkgver"
	local source_dir="$srcdir/ms912x-ng-$_upstream_version"

	install -dm755 "$pkgdir/usr/src"
	bsdtar --no-same-owner -xf "$srcdir/$dkms_name-$pkgver.tar.gz" -C "$pkgdir/usr/src"
	install -Dm644 "$source_dir/LICENSE" \
		"$pkgdir/usr/share/licenses/$pkgname/LICENSE"
	install -Dm644 "$source_dir/README.md" \
		"$pkgdir/usr/share/doc/$pkgname/README.md"
	install -Dm644 "$source_dir/AUTHORS" \
		"$pkgdir/usr/share/doc/$pkgname/AUTHORS"
	install -Dm644 "$dkms_dir/linux/udev/60-ms912x.rules" \
		"$pkgdir/usr/lib/udev/rules.d/60-ms912x.rules"
}
