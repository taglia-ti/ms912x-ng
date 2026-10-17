# Maintainer: Tiago Tagliaferri
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
unset _source_dir _git_revision _git_commit
pkgrel=1
pkgdesc="ms912x-ng DKMS DRM driver for MacroSilicon USB display adapters"
arch=('any')
url="https://github.com/rhgndf/ms912x"
license=('GPL-2.0-only')
depends=('dkms')
makedepends=('git')
optdepends=('linux-headers: build the module for the stock Arch kernel')
source=(
	Makefile
	VERSION
	LICENSE
	README.md
	AUTHORS
	ms912x.h
	ms912x_*.c
	ms912x_*.h
)
sha256sums=()
for _source in "${source[@]}"; do
	sha256sums+=('SKIP')
done
unset _source

package() {
	local source_dir="$srcdir"
	local dkms_dir="$pkgdir/usr/src/ms912x-ng-$pkgver"

	install -d "$dkms_dir"
	install -m 644 \
		"$source_dir/Makefile" \
		"$source_dir"/*.c \
		"$source_dir"/*.h \
		"$dkms_dir/"
	printf '%s\n' "$pkgver" > "$dkms_dir/VERSION"
	cat > "$dkms_dir/dkms.conf" <<EOF_DKMS
PACKAGE_NAME="ms912x-ng"
PACKAGE_VERSION="$pkgver"
CLEAN="make KVER=\${kernelver} clean"
MAKE[0]="make KVER=\${kernelver} all"
BUILT_MODULE_NAME[0]="ms912x"
BUILT_MODULE_LOCATION[0]=""
DEST_MODULE_LOCATION[0]="/kernel/drivers/gpu/drm/"
AUTOINSTALL="yes"
EOF_DKMS
	install -Dm644 "$source_dir/LICENSE" \
		"$pkgdir/usr/share/licenses/$pkgname/LICENSE"
	install -Dm644 "$source_dir/README.md" \
		"$pkgdir/usr/share/doc/$pkgname/README.md"
	install -Dm644 "$source_dir/AUTHORS" \
		"$pkgdir/usr/share/doc/$pkgname/AUTHORS"
}
