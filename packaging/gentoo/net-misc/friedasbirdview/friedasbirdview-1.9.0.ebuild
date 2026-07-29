# Copyright 2026 FriedasBirdview contributors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

inherit cmake xdg

MY_PN="FriedasBirdview"

DESCRIPTION="KDE Plasma Frigate activity companion"
HOMEPAGE="https://github.com/escapechen/FriedasBirdview"
SRC_URI="https://github.com/escapechen/${MY_PN}/archive/refs/tags/v${PV}.tar.gz -> ${P}.tar.gz"

S="${WORKDIR}/${MY_PN}-${PV}"

LICENSE="MIT"
SLOT="0"
KEYWORDS="~amd64"

RDEPEND="
	>=dev-qt/qtbase-6.5:6[X,gui,network,widgets]
	>=dev-qt/qtmultimedia-6.5:6
	>=dev-qt/qtwebchannel-6.5:6
	>=dev-qt/qtwebengine-6.5:6[widgets]
	>=dev-qt/qtwebsockets-6.5:6
	>=kde-frameworks/kwallet-6.0:6
	dev-libs/nss
	dev-libs/openssl:0=
"
DEPEND="${RDEPEND}"
BDEPEND=">=dev-build/cmake-3.21"

DOCS=(
	AUTHORS.md
	CHANGELOG.md
	README.md
	THIRD_PARTY_NOTICES.md
	docs/BUILD_FROM_SOURCE.md
)

src_configure() {
	cmake_src_configure
}

src_compile() {
	cmake_src_compile
}

src_test() {
	cmake_src_test
}

src_install() {
	cmake_src_install
}

pkg_postinst() {
	xdg_pkg_postinst
}

pkg_postrm() {
	xdg_pkg_postrm
}
