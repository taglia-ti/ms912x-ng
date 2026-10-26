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
unset _git_revision _git_commit
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
	linux/Makefile
	linux/Kbuild
	linux/ms912x*.c
	linux/ms912x*.h
	ms912x_*.h
	shared/ms912x_*.c
	shared/ms912x_*.h
)
_staged_module_sources=()
source=(
	dkms.conf
	VERSION
	LICENSE
	README.md
	AUTHORS
)
for _module_source in "${_module_sources[@]}"; do
	if [[ "$_module_source" == */* ]]; then
		_staged_source=".makepkg-source-${_module_source##*/}-$(sha256sum "$_source_dir/$_module_source" | cut -d ' ' -f1)"
		source+=("$_staged_source::file://$_source_dir/$_module_source")
	else
		_staged_source="$_module_source"
		source+=("$_module_source")
	fi
	_staged_module_sources+=("$_staged_source")
done
_udev_staged_source=".makepkg-source-60-ms912x.rules-$(sha256sum "$_source_dir/linux/udev/60-ms912x.rules" | cut -d ' ' -f1)"
source+=("$_udev_staged_source::file://$_source_dir/linux/udev/60-ms912x.rules")
unset _source_dir
sha256sums=()
for _source in "${source[@]}"; do
	sha256sums+=('SKIP')
done
unset _source

package() {
	local source_dir="$srcdir"
	local dkms_dir="$pkgdir/usr/src/ms912x-ng-$pkgver"
	local module_source staged_source index

	# makepkg stages local sources by basename; restore the module's layout.
	for index in "${!_module_sources[@]}"; do
		module_source="${_module_sources[index]}"
		staged_source="${_staged_module_sources[index]}"
		install -Dm644 "$source_dir/$staged_source" \
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
	install -Dm644 "$source_dir/$_udev_staged_source" \
		"$pkgdir/usr/lib/udev/rules.d/60-ms912x.rules"
}
