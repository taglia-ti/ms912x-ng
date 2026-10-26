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
_module_sources=(
	Makefile
	ms912x_*.c
	linux/Kbuild
	linux/ms912x*.c
	linux/ms912x*.h
	ms912x_*.h
	shared/ms912x_*.c
	shared/ms912x_*.h
)
source=(
	dkms.conf
	VERSION
	LICENSE
	README.md
	AUTHORS
	"${_module_sources[@]}"
	linux/udev/60-ms912x.rules
)
sha256sums=()
for _source in "${source[@]}"; do
	sha256sums+=('SKIP')
done
unset _source

package() {
	local source_dir="$srcdir"
	local dkms_dir="$pkgdir/usr/src/ms912x-ng-$pkgver"
	local module_source

	# makepkg stages local sources by basename; restore the module's layout.
	for module_source in "${_module_sources[@]}"; do
		install -Dm644 "$source_dir/${module_source##*/}" \
			"$dkms_dir/$module_source"
	done
	printf '%s\n' "$pkgver" > "$dkms_dir/VERSION"
	install -m644 "$source_dir/dkms.conf" "$dkms_dir/dkms.conf"
	install -Dm644 "$source_dir/LICENSE" \
		"$pkgdir/usr/share/licenses/$pkgname/LICENSE"
	install -Dm644 "$source_dir/README.md" \
		"$pkgdir/usr/share/doc/$pkgname/README.md"
	install -Dm644 "$source_dir/AUTHORS" \
		"$pkgdir/usr/share/doc/$pkgname/AUTHORS"
	install -Dm644 "$source_dir/60-ms912x.rules" \
		"$pkgdir/usr/lib/udev/rules.d/60-ms912x.rules"
}
