# Maintainer: Matteo Beniamino <m.beniamino@tautologica.org>

# Local build: compiles the aperi source tree containing this PKGBUILD
# (including uncommitted changes) instead of downloading a release.
# Run `makepkg -si` from the top level directory of the repository.

pkgname=aperi
pkgver=0.11.0pre
pkgrel=1
pkgdesc='Simple resource opener based on file extensions and URI schemas'
arch=('x86_64' 'aarch64')
url='https://codeberg.org/mbeniamino/aperi'
license=('MIT')
depends=('glibc' 'dbus')
makedepends=('meson' 'systemd')
optdepends=('python: example show_items.py ShowItems handler'
            'wl-clipboard: used by the example show_items.py')
install=aperi.install

# Version from meson.build; pacman versions can't contain '-', so
# 0.11.0-pre becomes 0.11.0pre (which sorts before 0.11.0)
pkgver() {
  sed -n "s/^project('aperi'.*version: *'\([^']*\)'.*/\1/p" "${startdir}/meson.build" | tr -d -
}

build() {
  arch-meson "${startdir}" build -Ddbus=enabled
  meson compile -C build
}

check() {
  meson test -C build --print-errorlogs
}

package() {
  meson install -C build --destdir "${pkgdir}"
  install -Dm644 "${startdir}/LICENSE" -t "${pkgdir}/usr/share/licenses/${pkgname}"
}
